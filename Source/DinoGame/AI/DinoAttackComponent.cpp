#include "AI/DinoAttackComponent.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoCreature.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UDinoAttackComponent::UDinoAttackComponent()
{
	// Ticks only while a phase asks for it (windup turning, lunge bite checks). Everything else
	// runs on timers, so an idle creature pays nothing.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);
}

void UDinoAttackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UDinoAttackComponent, Phase);
	DOREPLIFETIME(UDinoAttackComponent, HeldVictim);
}

void UDinoAttackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		TickPhase(DeltaTime);
	}
}

// --- Controller-facing -------------------------------------------------------------------------

bool UDinoAttackComponent::TryStartAttack(bool bForced)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || IsBusy() || GetTimeSeconds() < CooldownEndsAt)
	{
		return false;
	}

	ADinoCreature* Creature = GetCreature();
	ADinoAIControllerBase* AI = GetAIController();
	if (!Creature || !AI || Creature->IsDead())
	{
		return false;
	}

	if (!bForced && !CanTriggerFromAwareness(AI->GetAwareness()))
	{
		return false;
	}

	ADinoCharacter* Victim = SelectVictim(bForced);
	if (!Victim && !CanAttackWithoutVictim(bForced))
	{
		return false;
	}

	AttackTarget = Victim;

	// Stop the path following that got the creature here. Left running, the Hunting MoveTo
	// keeps steering the pawn until the StateTree processes the Attacking event a tick later,
	// and a lunge would start from a creature still sliding toward its old goal.
	AI->StopMovement();

	UE_LOG(LogDinoGame, Log, TEXT("%s attacking %s%s."), *Creature->GetName(),
		*GetNameSafe(Victim), bForced ? TEXT(" (forced)") : TEXT(""));

	BeginAttack(Victim);
	return true;
}

bool UDinoAttackComponent::BeginStagger(float Duration)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Duration <= 0.0f)
	{
		return false;
	}

	if (IsBusy() && Phase != EDinoAttackPhase::Staggered && !CanBeInterrupted())
	{
		UE_LOG(LogDinoGame, Log, TEXT("%s ignored a stagger during %s - this attack cannot be interrupted."),
			*Owner->GetName(), *UEnum::GetDisplayValueAsText(Phase).ToString());
		return false;
	}

	CleanUpAttack();
	ReleaseVictim();
	AttackTarget.Reset();

	if (ADinoAIControllerBase* AI = GetAIController())
	{
		AI->StopMovement();
	}

	if (const ADinoCreature* Creature = GetCreature())
	{
		if (UCharacterMovementComponent* Movement = Creature->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}

	UE_LOG(LogDinoGame, Log, TEXT("%s staggered for %.1fs."), *Owner->GetName(), Duration);

	// A stagger while already staggered restarts the clock rather than stacking - repeated hits
	// keep a creature reeling, but never lock it down longer than one stagger past the last hit.
	SetPhase(EDinoAttackPhase::Staggered, Duration);
	return true;
}

void UDinoAttackComponent::CancelAttack()
{
	if (IsBusy())
	{
		FinishAttack(EDinoAttackOutcome::Cancelled);
	}
}

float UDinoAttackComponent::GetCooldownRemaining() const
{
	return FMath::Max(0.0f, static_cast<float>(CooldownEndsAt - GetTimeSeconds()));
}

FString UDinoAttackComponent::DescribeAttack() const
{
	const float Cooldown = GetCooldownRemaining();
	if (!IsBusy() && Cooldown <= 0.0f)
	{
		return FString();
	}

	FString Text = FString::Printf(TEXT("attack: %s"), *UEnum::GetDisplayValueAsText(Phase).ToString());
	if (Cooldown > 0.0f)
	{
		Text += FString::Printf(TEXT("  cd %.1fs"), Cooldown);
	}
	if (HeldVictim)
	{
		Text += FString::Printf(TEXT("  holding %s"), *HeldVictim->GetName());
	}
	return Text;
}

void UDinoAttackComponent::DrawDebug(const UWorld* World, float Lifetime) const
{
	// A thick line from captor to victim, whatever the style: the one thing worth seeing at a
	// glance from across the map is who has whom.
	const AActor* Owner = GetOwner();
	if (World && Owner && HeldVictim)
	{
		DrawDebugLine(World, Owner->GetActorLocation(), HeldVictim->GetActorLocation(),
			FColor::Red, false, Lifetime, 0, 8.0f);
	}
}

void UDinoAttackComponent::AppendSetupCheck(TArray<FString>& OutLines) const
{
	// Nothing shared to check. The controller already reports that a component exists.
}

FString UDinoAttackComponent::CheckLine(bool bPass, const FString& Label, const FString& Detail)
{
	return FString::Printf(TEXT("%s %s%s"),
		bPass ? TEXT("[PASS]") : TEXT("[FAIL]"),
		*Label,
		Detail.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - %s"), *Detail));
}

// --- Phase machinery ---------------------------------------------------------------------------

void UDinoAttackComponent::OnPhaseTimerElapsed()
{
	if (Phase == EDinoAttackPhase::Staggered)
	{
		FinishAttack(EDinoAttackOutcome::Interrupted);
	}
}

void UDinoAttackComponent::SetPhase(EDinoAttackPhase NewPhase, float Duration, bool bTick)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(PhaseTimer);
	SetComponentTickEnabled(bTick);

	const EDinoAttackPhase OldPhase = Phase;
	Phase = NewPhase;
	PhaseStartedAt = GetTimeSeconds();

	if (Duration > 0.0f)
	{
		World->GetTimerManager().SetTimer(PhaseTimer, this, &UDinoAttackComponent::OnPhaseTimerElapsed, Duration, false);
	}

	if (OldPhase == NewPhase)
	{
		return;
	}

	// RepNotify never fires on the machine that wrote the value, so the server broadcasts for
	// itself. On a listen server the host is a player too, and would otherwise miss every cue.
	OnAttackPhaseChanged.Broadcast(NewPhase, OldPhase);

	if (OldPhase == EDinoAttackPhase::None)
	{
		OnAttackBegan.Broadcast(AttackTarget.Get());
	}
}

void UDinoAttackComponent::OnRep_Phase(EDinoAttackPhase OldPhase)
{
	OnAttackPhaseChanged.Broadcast(Phase, OldPhase);
}

void UDinoAttackComponent::FinishAttack(EDinoAttackOutcome Outcome)
{
	CleanUpAttack();
	ReleaseVictim();
	AttackTarget.Reset();

	CooldownEndsAt = GetTimeSeconds() + AttackCooldown;

	// Phase goes to None before the broadcast, so the controller's handler sees a creature that
	// is free and can set awareness without the engaged-in-melee guard refusing it.
	SetPhase(EDinoAttackPhase::None);

	UE_LOG(LogDinoGame, Log, TEXT("%s attack finished: %s."), *GetNameSafe(GetOwner()),
		*UEnum::GetDisplayValueAsText(Outcome).ToString());

	OnAttackFinished.Broadcast(Outcome);
}

// --- Victims -----------------------------------------------------------------------------------

void UDinoAttackComponent::HoldVictim(ADinoCharacter* Victim, EDinoRestraint Kind)
{
	if (!Victim)
	{
		return;
	}

	HeldVictim = Victim;
	Victim->BeginRestraint(GetCreature(), Kind);
}

void UDinoAttackComponent::ReleaseVictim()
{
	if (!HeldVictim)
	{
		return;
	}

	// Only end a restraint this creature owns. The victim may already have been released by
	// its own death, and must never be released out from under a different captor.
	if (IsValid(HeldVictim) && HeldVictim->GetRestrainingCreature() == GetCreature())
	{
		HeldVictim->EndRestraint();
	}

	HeldVictim = nullptr;
}

bool UDinoAttackComponent::IsValidVictim(const ADinoCharacter* Victim) const
{
	return IsValid(Victim)
		&& Victim->IsAlive()
		&& !Victim->IsRestrained()
		&& HasClearLineTo(Victim);
}

bool UDinoAttackComponent::HasClearLineTo(const AActor* Other) const
{
	const ADinoCreature* Creature = GetCreature();
	const UWorld* World = GetWorld();
	if (!Creature || !Other || !World)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DinoAttackLineOfSight), false, Creature);
	Params.AddIgnoredActor(Other);

	return !World->LineTraceTestByChannel(
		Creature->GetPawnViewLocation(), Other->GetActorLocation(), ECC_Visibility, Params);
}

void UDinoAttackComponent::DealDamage(ADinoCharacter* Victim, float Amount, const FVector& FromDirection) const
{
	if (!IsValid(Victim) || Amount <= 0.0f)
	{
		return;
	}

	// Point damage rather than plain so the direction travels with the hit - unused today, but
	// it is what a hit reaction or a knock-back will want, and it costs nothing to carry.
	UGameplayStatics::ApplyPointDamage(Victim, Amount, FromDirection, FHitResult(),
		GetAIController(), GetOwner(), UDamageType::StaticClass());
}

float UDinoAttackComponent::EdgeDistance2D(const AActor* Other) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Other)
	{
		return TNumericLimits<float>::Max();
	}

	return FVector::Dist2D(Owner->GetActorLocation(), Other->GetActorLocation())
		- Owner->GetSimpleCollisionRadius()
		- Other->GetSimpleCollisionRadius();
}

// --- Accessors -----------------------------------------------------------------------------

ADinoCreature* UDinoAttackComponent::GetCreature() const
{
	return Cast<ADinoCreature>(GetOwner());
}

ADinoAIControllerBase* UDinoAttackComponent::GetAIController() const
{
	const ADinoCreature* Creature = GetCreature();
	return Creature ? Cast<ADinoAIControllerBase>(Creature->GetController()) : nullptr;
}

float UDinoAttackComponent::GetCapsuleRadius() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetSimpleCollisionRadius() : 0.0f;
}

double UDinoAttackComponent::GetTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}
