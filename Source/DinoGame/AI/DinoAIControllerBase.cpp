#include "AI/DinoAIControllerBase.h"

#include "AI/DinoAttackComponent.h"
#include "AI/DinoCreature.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "Perception/AISense_Sight.h"
#include "GameFramework/PlayerState.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Components/StateTreeAIComponent.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/StateTreeAIComponent.h"

namespace
{
	/**
	 * Shortest gap between chase moves when the last one keeps ending immediately - a target on
	 * a ledge it cannot path to, say. Short enough that a real stall is never visible, long
	 * enough that an unreachable target costs a few queries a second rather than one a frame.
	 */
	constexpr double ChaseRestartInterval = 0.25;

	/**
	 * Rejects sentinel and garbage positions.
	 *
	 * AI perception reports FAISystem::InvalidLocation - FVector(FLT_MAX) - on some sight
	 * failure paths. Fed into a move request it is not an error anyone notices: with partial
	 * paths enabled the creature simply walks toward the edge of the world, which reads as
	 * "it turned the wrong way" rather than "the destination is 3.4e38".
	 *
	 * The threshold is far outside any real level but far below FLT_MAX.
	 */
	bool IsUsableWorldLocation(const FVector& Location)
	{
		return !Location.ContainsNaN() && Location.GetAbsMax() < 1.0e9f;
	}
}

ADinoAIControllerBase::ADinoAIControllerBase()
{
	// Must stay true, even though nothing here overrides Tick.
	//
	// AAIController::Tick is not idle work - it is the only caller of UpdateControlRotation,
	// which is what aims the controller's control rotation at the focus target, or copies the
	// pawn's facing when there is none. APawn::GetViewRotation returns that control rotation,
	// and AI perception takes its sight direction from it.
	//
	// So disabling this does not save a tick, it freezes the creature's vision at whatever
	// direction the controller spawned facing. The pawn still moves, still turns, still paths
	// correctly - component ticks are registered separately from the actor's - while its sight
	// cone stays pinned to its spawn orientation. It fails to see things in front of it and
	// spots things behind it, and nothing in the movement or perception setup looks wrong.
	PrimaryActorTick.bCanEverTick = true;

	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SetPerceptionComponent(*Perception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));

	// Affiliation is the classic silent failure here. It defaults to enemies only, and
	// with no team set up every player counts as neutral - so perception fires never,
	// with no warning and nothing in the log. Detect everything and filter ourselves in
	// IsValidTarget, where the rule is visible.
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*HearingConfig);
	Perception->SetDominantSense(SightConfig->GetSenseImplementation());

	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));

	// Started explicitly in OnPossess instead, so it runs only on the server and only once
	// there is actually a pawn to act on. Automatic start would fire on clients too, where
	// there is no AI to run.
	StateTreeAI->SetStartLogicAutomatically(false);
}

void ADinoAIControllerBase::BeginPlay()
{
	Super::BeginPlay();

	// Applied here rather than in the constructor so EditDefaultsOnly overrides set on a
	// Blueprint child are picked up; constructor values would be the C++ defaults only.
	if (SightConfig)
	{
		SightConfig->SightRadius = SightRadius;
		SightConfig->LoseSightRadius = FMath::Max(LoseSightRadius, SightRadius);
		SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngle;
		SightConfig->PointOfViewBackwardOffset = PointOfViewBackwardOffset;
		SightConfig->NearClippingRadius = NearClippingRadius;
		SightConfig->AutoSuccessRangeFromLastSeenLocation = AutoSuccessRangeFromLastSeen;
		SightConfig->SetMaxAge(MemoryDuration);
		Perception->ConfigureSense(*SightConfig);
	}

	if (HearingConfig)
	{
		HearingConfig->HearingRange = HearingRange;
		HearingConfig->SetMaxAge(MemoryDuration);
		Perception->ConfigureSense(*HearingConfig);
	}

	Perception->OnTargetPerceptionUpdated.AddDynamic(this, &ADinoAIControllerBase::HandlePerceptionUpdated);
}

void ADinoAIControllerBase::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// AI runs on the server only. A controller existing anywhere else is a bug, but log
	// it rather than assert - a playtest surviving is worth more than a clean crash.
	if (!HasAuthority())
	{
		UE_LOG(LogDinoGame, Warning, TEXT("%s possessed a pawn without authority; AI will not run."), *GetName());
		return;
	}

	if (StateTreeAI)
	{
		// An unassigned StateTree logs its own warning from the component. That warning is
		// worth leaving audible: it is the difference between "the creature has no brain"
		// and "the creature's brain is broken", which look identical in game.
		StateTreeAI->StartLogic();

		// StartLogic fails with one LogStateTree error and nothing else - when no tree is
		// assigned, or when the tree's schema names an AI Controller Class this controller does
		// not inherit from (a duplicated controller Blueprint is a sibling, not a child). The
		// creature then looks half-alive rather than broken: perception and attacks run from
		// C++ without the tree, so it notices you and lunges, but never walks or searches.
		if (!StateTreeAI->IsRunning())
		{
			DinoScreenError(FString::Printf(
				TEXT("%s: StateTree did not start. Either none is assigned, or the tree's schema ")
				TEXT("AI Controller Class is one this controller does not inherit from - set it to ")
				TEXT("DinoAIControllerBase. It will perceive and attack but never move."),
				*GetName()));
		}
	}

	// Found rather than required: a creature with no attack component simply never attacks,
	// which is the right answer for a herbivore.
	AttackComponent = InPawn ? InPawn->FindComponentByClass<UDinoAttackComponent>() : nullptr;
	if (AttackComponent)
	{
		AttackBeganHandle = AttackComponent->OnAttackBegan.AddUObject(this, &ADinoAIControllerBase::HandleAttackBegan);
		AttackFinishedHandle = AttackComponent->OnAttackFinished.AddUObject(this, &ADinoAIControllerBase::HandleAttackFinished);
	}

	GetWorldTimerManager().SetTimer(CombatTimer, this, &ADinoAIControllerBase::TickCombat, 0.1f, true);
}

void ADinoAIControllerBase::OnUnPossess()
{
	if (AttackComponent)
	{
		// Unbound before cancelling, so the cancel releases any victim without the finish
		// handler making awareness decisions for a creature that is losing its controller.
		AttackComponent->OnAttackBegan.Remove(AttackBeganHandle);
		AttackComponent->OnAttackFinished.Remove(AttackFinishedHandle);
		AttackComponent->CancelAttack();
		AttackComponent = nullptr;
	}

	GetWorldTimerManager().ClearTimer(CombatTimer);

	Super::OnUnPossess();
}

bool ADinoAIControllerBase::IsValidTarget(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	// Players only for now. Creature-versus-creature perception (a raptor noticing a
	// herbivore, birds scattering from a predator) is a later slice, and wants a faction
	// concept rather than this check.
	//
	// A player *character*, not any pawn with a player behind it. AI sight registers every pawn
	// in the world as visible by default (UAISense_Sight::bAutoRegisterAllPawnsAsSources), and
	// a spectator camera is a pawn possessed by a player - so the looser rule had creatures
	// hunting the host's spectator camera around the lobby.
	const ADinoCharacter* Player = Cast<ADinoCharacter>(Actor);
	if (!Player || Player->GetPlayerState() == nullptr || !Player->IsAlive())
	{
		return false;
	}

	// Held by someone else. Held by this creature is still a valid target - it is the one being
	// attacked.
	if (Player->IsRestrained() && Player->GetRestrainingCreature() != GetPawn())
	{
		return false;
	}

	return true;
}

void ADinoAIControllerBase::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!HasAuthority() || IsCreatureDead() || !IsValidTarget(Actor))
	{
		return;
	}

	const bool bBySight = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();

	if (Stimulus.WasSuccessfullySensed())
	{
		AcquireTarget(Actor, Stimulus.StimulusLocation, bBySight);
		return;
	}

	// Stimulus expired. Only sight loss matters: hearing stimuli always expire, and
	// treating that as "lost the target" would cancel a hunt every time a noise aged out.
	if (!bBySight || Actor != CurrentTarget)
	{
		return;
	}

	bHasLiveContact = false;

	// The actor's own location is the correct answer anyway: at the instant sight breaks,
	// where the target is *is* where it was last seen.
	LastKnownLocation = IsUsableWorldLocation(Stimulus.StimulusLocation)
		? Stimulus.StimulusLocation
		: Actor->GetActorLocation();

	// Captured at the moment of loss, not continuously: what matters is which way you were
	// going when you disappeared, which is the only clue the creature actually has.
	const FVector Velocity = Actor->GetVelocity();
	if (!Velocity.IsNearlyZero())
	{
		LastKnownDirection = Velocity.GetSafeNormal();
	}
	else if (const APawn* Self = GetPawn())
	{
		// A target that was standing still when it vanished gives no heading at all, and a zero
		// direction collapses the predicted location onto the last known one - which removes
		// the entire forward bias and leaves the creature searching uniformly, including
		// behind itself. "Further along the line from me to where you were" is a poor guess
		// but a much better one than none.
		LastKnownDirection = (LastKnownLocation - Self->GetActorLocation()).GetSafeNormal2D();
	}
	else
	{
		LastKnownDirection = FVector::ZeroVector;
	}

	// Seed the search at the last known location so the first leg always goes there, whatever
	// the query later decides.
	NextSearchPoint = LastKnownLocation;

	UE_LOG(LogDinoGame, Log, TEXT("%s lost sight of %s at %s"),
		*GetName(), *Actor->GetName(), *LastKnownLocation.ToCompactString());

	// Mid-attack, record the loss and stop there. HandleAttackFinished reads bHasLiveContact
	// and starts the search itself; doing it here would pull the creature out of its attack.
	if (IsEngagedInAttack())
	{
		return;
	}

	// Hold the hunt briefly. Only from Hunting: a Suspicious creature investigating a noise has
	// nothing to persist with, and giving it a grace period would let a sound alone keep it
	// locked on.
	if (HuntPersistenceAfterLosingSight > 0.0f && GetAwareness() == EDinoAwareness::Hunting)
	{
		GetWorldTimerManager().SetTimer(
			LostSightTimer, this, &ADinoAIControllerBase::BeginSearching,
			HuntPersistenceAfterLosingSight, false);
		return;
	}

	BeginSearching();
}

void ADinoAIControllerBase::AcquireTarget(AActor* Actor, const FVector& SensedLocation, bool bBySight)
{
	const bool bIsNewEncounter = CurrentTarget != Actor;

	CurrentTarget = Actor;
	LastKnownLocation = IsUsableWorldLocation(SensedLocation)
		? SensedLocation
		: Actor->GetActorLocation();
	bHasLiveContact = bBySight;

	GetWorldTimerManager().ClearTimer(MemoryTimer);

	// Regaining sight inside the persistence window cancels the pending drop, so a chase
	// through broken cover stays one continuous hunt rather than a string of short ones.
	GetWorldTimerManager().ClearTimer(LostSightTimer);

	// Keep the eyes on the target while the body follows the path. See bTrackTargetWithEyes:
	// without this the sight cone points along the route, not at the thing being hunted.
	if (bBySight && bTrackTargetWithEyes)
	{
		SetFocus(Actor);
	}

	// A noise is a direction, not an identification: it should make a creature curious,
	// not omniscient. Only sight escalates to a confirmed hunt.
	SetAwareness(bBySight ? EDinoAwareness::Hunting : EDinoAwareness::Suspicious);

	if (bIsNewEncounter)
	{
		UE_LOG(LogDinoGame, Log, TEXT("%s acquired %s by %s"),
			*GetName(), *Actor->GetName(), bBySight ? TEXT("sight") : TEXT("sound"));
		BP_OnTargetAcquired(Actor, bBySight);
	}
}

void ADinoAIControllerBase::BeginSearching()
{
	// Contact may have returned and gone again between the timer being set and it firing;
	// the acquire path clears this timer, but a target destroyed mid-window would not.
	if (!CurrentTarget)
	{
		return;
	}

	// The eyes stop tracking here. Past this point the creature genuinely does not know where
	// the target is, and leaving focus set would keep its sight cone pinned to a position it
	// has no right to know - the search would then be theatre over perfect information.
	ClearFocus(EAIFocusPriority::Gameplay);

	SetAwareness(EDinoAwareness::Searching);

	UE_LOG(LogDinoGame, Log, TEXT("%s giving up the chase, searching near %s"),
		*GetName(), *LastKnownLocation.ToCompactString());

	BP_OnContactLost(CurrentTarget, LastKnownLocation);

	// Started here rather than at the moment of loss so the persistence window does not come
	// out of the player's escape window.
	GetWorldTimerManager().SetTimer(
		MemoryTimer, this, &ADinoAIControllerBase::ForgetTarget, MemoryDuration, false);
}

void ADinoAIControllerBase::ForgetTarget()
{
	if (bHasLiveContact)
	{
		return;
	}

	UE_LOG(LogDinoGame, Log, TEXT("%s gave up searching."), *GetName());

	ClearFocus(EAIFocusPriority::Gameplay);
	CurrentTarget = nullptr;
	SetAwareness(EDinoAwareness::Unaware);
	BP_OnTargetForgotten();
}

EDinoAwareness ADinoAIControllerBase::GetAwareness() const
{
	const ADinoCreature* Creature = Cast<ADinoCreature>(GetPawn());
	return Creature ? Creature->GetAwareness() : EDinoAwareness::Unaware;
}

void ADinoAIControllerBase::SetAwareness(EDinoAwareness NewAwareness)
{
	// Perception keeps running while locked - the creature still tracks targets, it just
	// does not get to change state. That way releasing the lock resumes from reality
	// rather than from whatever was true when the lock was applied.
	if (bAwarenessLocked)
	{
		return;
	}

	// The same idea for attacks: see IsEngagedInAttack. HandleAttackFinished runs after the
	// component is free again, so it is never blocked by this.
	if (IsEngagedInAttack() && NewAwareness != EDinoAwareness::Attacking)
	{
		return;
	}

	ApplyAwareness(NewAwareness);
}

void ADinoAIControllerBase::DebugForceAwareness(EDinoAwareness NewAwareness)
{
	bAwarenessLocked = true;
	ApplyAwareness(NewAwareness);

	DinoScreenLog(FString::Printf(TEXT("%s forced to %s (locked)"),
		*GetName(), *DinoAwarenessName(NewAwareness)),
		DinoAwarenessColor(NewAwareness), 8.0f);
}

void ADinoAIControllerBase::DebugReleaseAwareness()
{
	if (!bAwarenessLocked)
	{
		return;
	}

	bAwarenessLocked = false;

	// Re-derive from what perception currently knows, rather than leaving the forced state
	// sitting there until the next stimulus happens to arrive. Mid-attack, stay in Attacking and
	// let the attack's own finish decide - leaving now would cancel it.
	ApplyAwareness(IsEngagedInAttack()
		? EDinoAwareness::Attacking
		: (bHasLiveContact
			? EDinoAwareness::Hunting
			: (CurrentTarget ? EDinoAwareness::Searching : EDinoAwareness::Unaware)));

	DinoScreenLog(FString::Printf(TEXT("%s released to perception"), *GetName()), FColor::White, 8.0f);
}

void ADinoAIControllerBase::ApplyAwareness(EDinoAwareness NewAwareness)
{
	ADinoCreature* Creature = Cast<ADinoCreature>(GetPawn());
	if (!Creature || Creature->GetAwareness() == NewAwareness)
	{
		return;
	}

	// A fresh search starts narrow again; without this the radius stays wide from the last one.
	if (NewAwareness == EDinoAwareness::Searching && Creature->GetAwareness() != EDinoAwareness::Searching)
	{
		LegsThisSearch = 0;
	}

	Creature->SetAwareness(NewAwareness);

	// The StateTree drives off this event rather than reading the enum every tick. Sent
	// after the pawn is updated, so anything the tree reads on entry sees the new value.
	if (StateTreeAI)
	{
		const FGameplayTag Tag = DinoAwarenessTags::FromAwareness(NewAwareness);
		if (Tag.IsValid())
		{
			StateTreeAI->SendStateTreeEvent(Tag);
		}
	}
}

FString ADinoAIControllerBase::DescribeState() const
{
	const ADinoCreature* Creature = Cast<ADinoCreature>(GetPawn());
	const FName Species = Creature && !Creature->SpeciesName.IsNone()
		? Creature->SpeciesName
		: FName(Creature ? *Creature->GetClass()->GetName() : TEXT("<no pawn>"));

	FString AttackText = AttackComponent ? AttackComponent->DescribeAttack() : FString();
	if (bChasing)
	{
		AttackText = AttackText.IsEmpty() ? DescribeChase() : AttackText + TEXT(" | ") + DescribeChase();
	}

	return FString::Printf(TEXT("%s: %s%s | target=%s | contact=%s | lastKnown=%s | goal=%s%s%s"),
		*Species.ToString(),
		*DinoAwarenessName(GetAwareness()),
		bAwarenessLocked ? TEXT(" [LOCKED]") : TEXT(""),
		CurrentTarget ? *CurrentTarget->GetName() : TEXT("none"),
		bHasLiveContact ? TEXT("live") : TEXT("memory"),
		CurrentTarget ? *LastKnownLocation.ToCompactString() : TEXT("-"),
		*ActiveSearchDestination.ToCompactString(),
		AttackText.IsEmpty() ? TEXT("") : TEXT(" | "),
		*AttackText);
}

// --- Combat ----------------------------------------------------------------------------------

bool ADinoAIControllerBase::IsEngagedInAttack() const
{
	return AttackComponent && AttackComponent->IsBusy();
}

bool ADinoAIControllerBase::IsCreatureDead() const
{
	const ADinoCreature* Creature = Cast<ADinoCreature>(GetPawn());
	return Creature && Creature->IsDead();
}

void ADinoAIControllerBase::TickCombat()
{
	if (IsCreatureDead())
	{
		return;
	}

	// A target that died or was grabbed by another creature mid-hunt. Mid-attack, leave it to
	// HandleAttackFinished instead, so the attack plays out rather than snapping to idle.
	if (CurrentTarget && !IsEngagedInAttack() && !IsValidTarget(CurrentTarget))
	{
		DropCurrentTarget(TEXT("target is no longer valid"));
	}

	// Under a debug lock the StateTree task drives attacks instead, so a forced state is not
	// fought by the normal triggers.
	if (!AttackComponent || bAwarenessLocked)
	{
		return;
	}

	AttackComponent->TryStartAttack(false);
}

void ADinoAIControllerBase::HandleAttackBegan(ADinoCharacter* Victim)
{
	// Null for a stagger. For a real attack the victim becomes the target, whoever the
	// creature was chasing before - a devour can catch someone it had not even noticed.
	if (Victim)
	{
		CurrentTarget = Victim;
		LastKnownLocation = Victim->GetActorLocation();

		// Whatever was counting down toward giving up no longer applies: it has someone.
		GetWorldTimerManager().ClearTimer(MemoryTimer);
		GetWorldTimerManager().ClearTimer(LostSightTimer);

		if (bTrackTargetWithEyes)
		{
			SetFocus(Victim);
		}
	}

	SetAwareness(EDinoAwareness::Attacking);
}

void ADinoAIControllerBase::HandleAttackFinished(EDinoAttackOutcome Outcome)
{
	UE_LOG(LogDinoGame, Log, TEXT("%s attack over (%s), target %s, contact %s."), *GetName(),
		*UEnum::GetDisplayValueAsText(Outcome).ToString(), *GetNameSafe(CurrentTarget),
		bHasLiveContact ? TEXT("live") : TEXT("lost"));

	// A locked state stays where the debug command put it; the task will attack again.
	if (IsCreatureDead() || bAwarenessLocked)
	{
		return;
	}

	if (CurrentTarget && !IsValidTarget(CurrentTarget))
	{
		DropCurrentTarget(Outcome == EDinoAttackOutcome::Killed ? TEXT("killed it") : TEXT("target no longer valid"));
		return;
	}

	if (!CurrentTarget)
	{
		DropCurrentTarget(TEXT("no target after attack"));
		return;
	}

	if (bHasLiveContact)
	{
		SetAwareness(EDinoAwareness::Hunting);
		if (bTrackTargetWithEyes)
		{
			SetFocus(CurrentTarget);
		}
		return;
	}

	// Sight was lost during the attack - the loss was recorded but deliberately not acted on.
	BeginSearching();
}

void ADinoAIControllerBase::DropCurrentTarget(const TCHAR* Reason)
{
	UE_LOG(LogDinoGame, Log, TEXT("%s dropping %s: %s."), *GetName(), *GetNameSafe(CurrentTarget), Reason);

	GetWorldTimerManager().ClearTimer(MemoryTimer);
	GetWorldTimerManager().ClearTimer(LostSightTimer);
	ClearFocus(EAIFocusPriority::Gameplay);

	CurrentTarget = nullptr;
	bHasLiveContact = false;

	// In co-op the next player is usually standing right there. Without this the creature
	// would go idle and stare at them until they happened to leave its sight and come back,
	// because perception only reports changes.
	if (TryAcquireVisibleTarget())
	{
		return;
	}

	SetAwareness(EDinoAwareness::Unaware);
	BP_OnTargetForgotten();
}

bool ADinoAIControllerBase::TryAcquireVisibleTarget()
{
	const APawn* Self = GetPawn();
	if (!Perception || !Self)
	{
		return false;
	}

	TArray<AActor*> InSight;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), InSight);

	AActor* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();

	for (AActor* Candidate : InSight)
	{
		if (!IsValidTarget(Candidate))
		{
			continue;
		}

		const float Distance = FVector::DistSquared(Self->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Candidate;
		}
	}

	if (!Nearest)
	{
		return false;
	}

	AcquireTarget(Nearest, Nearest->GetActorLocation(), true);
	return true;
}

// --- Chasing ----------------------------------------------------------------------------------

void ADinoAIControllerBase::MaintainChase(const FAIMoveRequest& Request, bool bForceNewMove)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bGoalChanged = Request.GetGoalActor() != ChaseGoal.Get();
	const bool bMoveEnded = GetMoveStatus() == EPathFollowingStatus::Idle;

	bChasing = true;

	if (!bForceNewMove && !bGoalChanged)
	{
		if (!bMoveEnded || Now - LastChaseMoveTime < ChaseRestartInterval)
		{
			return;
		}

		// A move ended on its own and the creature is still hunting - exactly the case that
		// used to stall. Counted, because a rising number is the proof this is being hit.
		++ChaseRestarts;
	}

	const FPathFollowingRequestResult Result = MoveTo(Request);

	ChaseGoal = Request.GetGoalActor();
	LastChaseMoveTime = Now;
	++ChaseMovesIssued;

	switch (Result.Code)
	{
	case EPathFollowingRequestResult::Failed:            LastChaseRequest = TEXT("no path"); break;
	case EPathFollowingRequestResult::AlreadyAtGoal:     LastChaseRequest = TEXT("already there"); break;
	case EPathFollowingRequestResult::RequestSuccessful: LastChaseRequest = TEXT("moving"); break;
	default:                                              LastChaseRequest = TEXT("?"); break;
	}

	// Partial means the target is somewhere this creature cannot actually reach, so it is
	// heading for the nearest point it can. Surfaced because it looks identical to a creature
	// that is simply bad at pathing.
	const UPathFollowingComponent* PathFollowing = GetPathFollowingComponent();
	const FNavPathSharedPtr Path = PathFollowing ? PathFollowing->GetPath() : nullptr;
	bChasePathPartial = Result.Code == EPathFollowingRequestResult::RequestSuccessful
		&& Path.IsValid() && Path->IsPartial();
}

void ADinoAIControllerBase::StopChase()
{
	if (!bChasing)
	{
		return;
	}

	bChasing = false;
	ChaseGoal.Reset();
	StopMovement();
}

FString ADinoAIControllerBase::DescribeChase() const
{
	return FString::Printf(TEXT("chase: %d moves (%d restarts)  %s%s  last end: %s"),
		ChaseMovesIssued, ChaseRestarts, *LastChaseRequest,
		bChasePathPartial ? TEXT(" PARTIAL") : TEXT(""),
		*LastMoveEnd);
}

void ADinoAIControllerBase::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	LastMoveEnd = Result.ToString();
}

bool ADinoAIControllerBase::TryStartForcedAttack()
{
	return AttackComponent && AttackComponent->TryStartAttack(true);
}

void ADinoAIControllerBase::CancelAttackFromStateTree()
{
	if (AttackComponent)
	{
		AttackComponent->CancelAttack();
	}
}

void ADinoAIControllerBase::HandleCreatureDied()
{
	GetWorldTimerManager().ClearTimer(CombatTimer);
	GetWorldTimerManager().ClearTimer(MemoryTimer);
	GetWorldTimerManager().ClearTimer(LostSightTimer);
	GetWorldTimerManager().ClearTimer(SearchLegTimer);

	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);

	CurrentTarget = nullptr;
	bHasLiveContact = false;

	if (StateTreeAI)
	{
		StateTreeAI->StopLogic(TEXT("Creature died"));
	}

	// Past the lock and the engaged guard: nothing a dead creature was doing should survive.
	ApplyAwareness(EDinoAwareness::Unaware);
}

// --- Searching -------------------------------------------------------------------------------

FVector ADinoAIControllerBase::GetPredictedTargetLocation() const
{
	// With no heading this collapses to the last known location, which is the right answer:
	// a target that was standing still gives no directional clue at all.
	return LastKnownLocation + LastKnownDirection * HeadingPredictionDistance;
}

void ADinoAIControllerBase::RequestNextSearchPoint()
{
	if (!HasAuthority())
	{
		return;
	}

	// Called at the start of each leg, so this doubles as the leg's watchdog.
	GetWorldTimerManager().SetTimer(
		SearchLegTimer, this, &ADinoAIControllerBase::AbandonSearchLeg, SearchLegTimeout, false);

	// Pick synchronously first, every time. EQS is asynchronous and may never return - an
	// unassigned query, a query that fails, or one whose callback is simply late - and the
	// previous design left NextSearchPoint unchanged in all of those cases. The creature
	// then re-targeted the spot it was already standing on, completed the move instantly, and
	// span. Progress must not depend on a callback arriving.
	FVector Immediate;
	if (PickFallbackSearchPoint(Immediate))
	{
		NextSearchPoint = Immediate;
		NextPointSource = TEXT("fallback");
	}

	if (!SearchQuery)
	{
		// Distinct from a query that ran and returned nothing. Without separating these, an
		// unassigned asset and a broken query look identical in the readout.
		LastQueryOutcome = TEXT("no query assigned");
		return;
	}

	// The querier must be the pawn, not this controller.
	//
	// A controller is spawned at its pawn's location and then never moves: AController sets
	// bAttachToPawn = false, so its transform is frozen at the spawn point for the whole game.
	// Any test using the built-in Querier context therefore measured from where the creature
	// first appeared rather than where it is - which made the Trace test's line of sight a
	// permanent no-op and silently biased every search back toward the spawn area.
	//
	// The engine converts controller owners to pawns for exactly this reason, but only on the
	// Blueprint RunEQSQuery path; FEnvQueryRequest takes the owner verbatim. It warns through
	// the Visual Logger only, so nothing appears in the log.
	APawn* Querier = GetPawn();
	if (!Querier)
	{
		LastQueryOutcome = TEXT("no pawn to query from");
		return;
	}

	FEnvQueryRequest Request(SearchQuery, Querier);

	// RandomBest25Pct rather than SingleResult on purpose: the single best point is
	// deterministic, so a player who learns one hiding spot beats the creature forever.
	const int32 RequestId = Request.Execute(
		EEnvQueryRunMode::RandomBest25Pct, this, &ADinoAIControllerBase::OnSearchQueryFinished);

	if (RequestId == INDEX_NONE)
	{
		// The manager refused to start it - usually a query with no generator, or one whose
		// schema does not match how it is being run.
		LastQueryOutcome = TEXT("Execute() refused the query");
		return;
	}

	// Overwritten by OnSearchQueryFinished. If the readout stays on this line, the query
	// started but its callback never came back.
	LastQueryOutcome = FString::Printf(TEXT("running #%d..."), RequestId);
}

void ADinoAIControllerBase::OnSearchQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	if (Result.IsValid() && Result->IsSuccessful() && Result->Items.Num() > 0)
	{
		const FVector Point = Result->GetItemAsLocation(0);
		if (IsUsableWorldLocation(Point))
		{
			// Count survivors rather than reporting Items.Num().
			//
			// Items.Num() is not the number of points that passed the filters. For the
			// RandomBest modes the engine picks one item and then, when USE_EQS_DEBUGGER is
			// set, deliberately leaves every other item in the array so the debugger can
			// still draw them - failed ones included. That macro is on for everything except
			// Shipping and Test, so the raw count reads as the generator total in the editor
			// and in Development packages alike, and collapses to 1 only in a Shipping build.
			// It is never the answer to "how many points passed the filters".
			//
			// Discarded items keep their slot with bIsDiscarded set, so IsValid() is the
			// real test. Reported as kept/total because a filter that removes nothing and a
			// filter that removes everything are both bugs, and one number cannot tell them apart.
			int32 KeptItems = 0;
			for (const FEnvQueryItem& Item : Result->Items)
			{
				if (Item.IsValid())
				{
					++KeptItems;
				}
			}

			LastQueryOutcome = FString::Printf(TEXT("ok (%d/%d kept)"), KeptItems, Result->Items.Num());
			NextSearchPoint = Point;
			NextPointSource = TEXT("eqs");
			return;
		}

		LastQueryOutcome = TEXT("bad location");
	}
	else if (!Result.IsValid())
	{
		LastQueryOutcome = TEXT("no result");
	}
	else if (!Result->IsSuccessful())
	{
		// Split out, because "failed" covers four unrelated situations and only the first is
		// something to tune.
		//
		// Failed is the one that matters: FinalizeQuery marks the query Failed and empties
		// Items when NumValidItems reaches zero, so "every candidate was filtered out"
		// arrives here rather than as a successful result holding no items. Usually the Trace
		// or Pathfinding test being too strict. (World teardown also marks queries Failed, but
		// nobody is reading a debug readout at that point.)
		//
		// Aborted and OwnerLost are lifecycle, not tuning: the querier was torn down or the
		// pawn died while the query was still in flight. Treating those as a filtering problem
		// would send the next tuning pass chasing nothing.
		switch (Result->GetRawStatus())
		{
		case EEnvQueryStatus::Failed:
			LastQueryOutcome = TEXT("no candidate survived the filters");
			break;
		case EEnvQueryStatus::Aborted:
			LastQueryOutcome = TEXT("aborted");
			break;
		case EEnvQueryStatus::OwnerLost:
			LastQueryOutcome = TEXT("owner lost");
			break;
		default:
			LastQueryOutcome = FString::Printf(TEXT("failed, status %d"),
				static_cast<int32>(Result->GetRawStatus()));
			break;
		}
	}
	else
	{
		// Not reachable in practice - a query with no surviving items is marked Failed above
		// rather than succeeding with an empty list. Kept so the chain stays total.
		LastQueryOutcome = TEXT("0 items");
	}

	// A query that returns nothing is normal, not an error: every candidate may have been
	// filtered out. Wander instead of freezing.
	FVector Fallback;
	if (PickFallbackSearchPoint(Fallback))
	{
		NextSearchPoint = Fallback;
	}
}

bool ADinoAIControllerBase::PickFallbackSearchPoint(FVector& OutPoint) const
{
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!Nav)
	{
		return false;
	}

	FNavLocation Found;
	const APawn* Self = GetPawn();
	if (!Self)
	{
		return false;
	}

	const FVector Origin = IsUsableWorldLocation(LastKnownLocation) ? LastKnownLocation : Self->GetActorLocation();

	// A few attempts, because a point too close to where we already are produces a zero-length
	// leg that completes instantly - which is exactly the spin this is here to stop.
	const float MinLegDistance = 400.0f;
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
	if (Nav->GetRandomReachablePointInRadius(Origin, CurrentSearchRadius(), Found))
		{
			if (FVector::Dist2D(Found.Location, Self->GetActorLocation()) >= MinLegDistance)
			{
				OutPoint = Found.Location;
				return true;
			}
		}
	}

	return false;
}

void ADinoAIControllerBase::AbandonSearchLeg()
{
	if (GetAwareness() != EDinoAwareness::Searching)
	{
		return;
	}

	// Stopping the move makes the Search Step task finish, which completes the state and fires
	// the loop transition - so the next leg starts through the normal path rather than through
	// a special case.
	UE_LOG(LogDinoGame, Log, TEXT("%s abandoned a search leg after %.1fs, picking another."),
		*GetName(), SearchLegTimeout);

	StopMovement();
}

// --- Diagnostics -----------------------------------------------------------------------------

void ADinoAIControllerBase::NotifySearchLegStarted()
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double SinceLast = Now - LastLegStartTime;

	// A search leg should take seconds. Legs arriving in milliseconds mean the destination is
	// already reached on arrival and the state is looping on itself - which is invisible in
	// game, since the creature simply stands still. Say so loudly rather than let it hide.
	if (LastLegStartTime > 0.0 && SinceLast < 0.05)
	{
		++ConsecutiveFastLegs;
		if (ConsecutiveFastLegs == 20)
		{
			UE_LOG(LogDinoGame, Error,
				TEXT("%s: search legs are restarting instantly (%d in a row). The destination is ")
				TEXT("almost certainly inside the acceptance radius, so every move completes on ")
				TEXT("arrival. Check goal= in DinoAIStatus against the creature's own position."),
				*GetName(), ConsecutiveFastLegs);
		}
	}
	else
	{
		ConsecutiveFastLegs = 0;
	}

	++SearchLegCount;
	++LegsThisSearch;
	LastLegStartTime = Now;
}

void ADinoAIControllerBase::NotifySearchTick()
{
	++SearchTickCount;
}

FString ADinoAIControllerBase::DescribeSearchDiagnostics() const
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double InLeg = LastLegStartTime > 0.0 ? Now - LastLegStartTime : 0.0;

	if (ConsecutiveFastLegs >= 20)
	{
		return FString::Printf(TEXT("leg %d  SPINNING (%d instant legs)  ticks=%d"),
			SearchLegCount, ConsecutiveFastLegs, SearchTickCount);
	}

	return FString::Printf(TEXT("leg %d (%d this search, r=%.0f)  t=%.1fs  ticks=%d\nq: %s\nsrc=%s  eqs=%d fallback=%d"),
		SearchLegCount, LegsThisSearch, CurrentSearchRadius(), InLeg, SearchTickCount, *LastQueryOutcome,
		*ActivePointSource, LegsFromQuery, LegsFromFallback);
}

void ADinoAIControllerBase::RunSetupCheck(TArray<FString>& OutLines) const
{
	auto Report = [&OutLines](bool bPass, const FString& Label, const FString& Detail)
	{
		OutLines.Add(FString::Printf(TEXT("%s %s%s"),
			bPass ? TEXT("[PASS]") : TEXT("[FAIL]"),
			*Label,
			Detail.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - %s"), *Detail)));
	};

	const APawn* MyPawn = GetPawn();
	Report(MyPawn != nullptr, TEXT("possessing a pawn"),
		MyPawn ? MyPawn->GetName() : FString(TEXT("Auto Possess AI not set?")));

	const ADinoCreature* Creature = Cast<ADinoCreature>(MyPawn);
	Report(Creature != nullptr, TEXT("pawn is a DinoCreature"), TEXT(""));

	// A tree that is not Running means every state is inert, whatever the states contain.
	const EStateTreeRunStatus TreeStatus = StateTreeAI ? StateTreeAI->GetStateTreeRunStatus() : EStateTreeRunStatus::Unset;
	Report(TreeStatus == EStateTreeRunStatus::Running, TEXT("StateTree running"),
		!StateTreeAI
			? FString(TEXT("no component"))
			: TreeStatus == EStateTreeRunStatus::Running
				? FString(TEXT("Running"))
				: FString::Printf(TEXT("%s - no tree assigned, or its schema AI Controller Class does not match this controller"),
					*UEnum::GetDisplayValueAsText(TreeStatus).ToString()));

	Report(SearchQuery != nullptr, TEXT("search EQS query assigned"),
		SearchQuery ? SearchQuery->GetName() : FString(TEXT("using random-wander fallback")));

	// Printed rather than validated: the matching value lives in a binary EQS asset that
	// nothing here can read, so the most this can do is state the number the asset should
	// carry. Reported as a pass whenever an eye height is configured at all.
	if (Creature)
	{
		const float ContextOffset = Creature->GetEQSContextHeightOffset();
		Report(Creature->EyeHeightAboveFeet > 0.0f, TEXT("eye height configured"),
			FString::Printf(TEXT("eyes %.0f above feet; set the Trace test Context Height Offset to %.0f"),
				Creature->GetEyeHeightAboveFeet(), ContextOffset));
	}

	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	Report(Nav != nullptr, TEXT("navigation system present"), TEXT(""));

	// Standing on navmesh is the difference between "cannot path" and "will not path".
	if (Nav && MyPawn)
	{
		FNavLocation Projected;
		const bool bOnNav = Nav->ProjectPointToNavigation(MyPawn->GetActorLocation(), Projected, FVector(300.0f));
		Report(bOnNav, TEXT("creature is on navmesh"),
			bOnNav ? TEXT("") : TEXT("no NavMeshBoundsVolume here, or agent size excludes it"));
	}

	Report(!bAwarenessLocked, TEXT("awareness not debug-locked"),
		bAwarenessLocked ? TEXT("DinoSetState auto to release") : TEXT(""));

	Report(Perception != nullptr, TEXT("perception component"), TEXT(""));

	// A FAIL rather than a note, because every creature this check exists for is a predator.
	// A herbivore will fail it by design - read it as "will never attack", not as broken.
	Report(AttackComponent != nullptr, TEXT("attack component"),
		AttackComponent
			? AttackComponent->GetClass()->GetName()
			: FString(TEXT("none on the pawn - this creature will never attack")));
	if (AttackComponent)
	{
		AttackComponent->AppendSetupCheck(OutLines);
	}
	if (Creature)
	{
		OutLines.Add(FString::Printf(TEXT("vulnerability: %s"), *Creature->DescribeVulnerability()));
	}

	// Checked explicitly because its failure is silent and total: with the controller not
	// ticking, UpdateControlRotation never runs and the sight cone stays frozen at the spawn
	// direction while everything else - movement, pathing, the perception component itself -
	// carries on working normally.
	Report(PrimaryActorTick.IsTickFunctionEnabled(), TEXT("controller ticking"),
		PrimaryActorTick.IsTickFunctionEnabled()
			? FString::Printf(TEXT("view yaw %.0f, pawn yaw %.0f"),
				GetControlRotation().Yaw, MyPawn ? MyPawn->GetActorRotation().Yaw : 0.0f)
			: FString(TEXT("sight direction is frozen at spawn")));

	OutLines.Add(FString::Printf(TEXT("state: %s"), *DescribeState()));
	OutLines.Add(DescribeSearchDiagnostics().Replace(TEXT("\n"), TEXT("  ")));
}

float ADinoAIControllerBase::CurrentSearchRadius() const
{
	const float Multiplier = FMath::Min(
		1.0f + LegsThisSearch * SearchRadiusGrowthPerLeg,
		SearchRadiusMaxMultiplier);

	return FallbackSearchRadius * Multiplier;
}

void ADinoAIControllerBase::SetActiveSearchDestination(const FVector& Destination)
{
	ActiveSearchDestination = Destination;
	ActivePointSource = NextPointSource;

	if (ActivePointSource == TEXT("eqs"))
	{
		++LegsFromQuery;
	}
	else
	{
		++LegsFromFallback;
	}

	RecordLeg(Destination);
}

void ADinoAIControllerBase::RecordLeg(const FVector& Destination)
{
	FSearchLegRecord Record;
	Record.Leg = SearchLegCount;
	Record.Source = ActivePointSource;
	Record.QueryOutcome = LastQueryOutcome;
	Record.SearchRadius = CurrentSearchRadius();
	Record.DistanceFromCentre = FVector::Dist2D(Destination, LastKnownLocation);
	Record.LegLength = GetPawn() ? FVector::Dist2D(Destination, GetPawn()->GetActorLocation()) : -1.0f;

	LegHistory.Insert(Record, 0);
	if (LegHistory.Num() > MaxLegHistory)
	{
		LegHistory.SetNum(MaxLegHistory);
	}
}

void ADinoAIControllerBase::DescribeSearchHistory(TArray<FString>& OutLines) const
{
	if (LegHistory.Num() == 0)
	{
		OutLines.Add(TEXT("  no search legs recorded yet"));
		return;
	}

	OutLines.Add(TEXT("  leg   src       len    fromCentre  radius   query"));
	for (const FSearchLegRecord& Record : LegHistory)
	{
		OutLines.Add(FString::Printf(TEXT("  %-5d %-9s %-6.0f %-11.0f %-8.0f %s"),
			Record.Leg,
			*Record.Source,
			Record.LegLength,
			Record.DistanceFromCentre,
			Record.SearchRadius,
			*Record.QueryOutcome));
	}
}
