#include "AI/DinoLungeAttackComponent.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoCreature.h"
#include "Components/CapsuleComponent.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "TimerManager.h"

namespace
{
	/**
	 * Sustained movement during a windup. Some velocity is expected for a moment after the
	 * attack stops path following, while the creature brakes; this ignores that settling time.
	 */
	constexpr float WindupSettleSeconds = 0.2f;
	constexpr float WindupMovementSpeed = 20.0f;
}

bool UDinoLungeAttackComponent::CanTriggerFromAwareness(EDinoAwareness Awareness) const
{
	// The windup aims at a target, so there has to be one it is actively hunting.
	return Awareness == EDinoAwareness::Hunting;
}

ADinoCharacter* UDinoLungeAttackComponent::SelectVictim(bool bForced) const
{
	const ADinoAIControllerBase* AI = GetAIController();
	ADinoCharacter* Target = AI ? Cast<ADinoCharacter>(AI->GetCurrentTarget()) : nullptr;
	if (!Target || !IsValidVictim(Target))
	{
		return nullptr;
	}

	// Live sight, not memory. Without this it could lunge during hunt persistence, when it is
	// allowed to track a target's position through cover - an attack aimed through a wall.
	if (!bForced && !AI->HasLiveContact())
	{
		return nullptr;
	}

	return EdgeDistance2D(Target) <= AttackTriggerRange ? Target : nullptr;
}

void UDinoLungeAttackComponent::BeginAttack(ADinoCharacter* /*Victim*/)
{
	SetPhase(EDinoAttackPhase::Windup, WindupDuration, true);
}

void UDinoLungeAttackComponent::TickPhase(float DeltaTime)
{
	ADinoCreature* Creature = GetCreature();
	if (!Creature)
	{
		return;
	}

	if (Phase == EDinoAttackPhase::Windup)
	{
		// Turned explicitly because nothing else will: bOrientRotationToMovement only rotates a
		// creature that is accelerating, and a stationary one in windup never would. The turn
		// rate is the creature's own, so a player who circles faster than it turns is safe.
		if (const ADinoCharacter* Target = AttackTarget.Get())
		{
			const FVector ToTarget = (Target->GetActorLocation() - Creature->GetActorLocation()).GetSafeNormal2D();
			if (!ToTarget.IsNearlyZero())
			{
				const FRotator Current = Creature->GetActorRotation();
				const FRotator Wanted(0.0f, ToTarget.Rotation().Yaw, 0.0f);
				Creature->SetActorRotation(FMath::RInterpConstantTo(
					Current, Wanted, DeltaTime, Creature->TurnRateDegreesPerSecond));
			}
		}

		if (GetTimeSeconds() - PhaseStartedAt > WindupSettleSeconds
			&& Creature->GetVelocity().Size2D() > WindupMovementSpeed)
		{
			++WindupMovementFrames;
		}
	}
	else if (Phase == EDinoAttackPhase::Lunge)
	{
		TryBite();

		// Landed short - on a rise, or a ledge - and caught nobody: the jump is over. Without this
		// it would skid along the ground for the rest of the jump's time. Only from halfway on,
		// because for the first frame or two of a jump it is still standing on the floor it left.
		const UCharacterMovementComponent* Movement = Creature->GetCharacterMovement();
		if (Phase == EDinoAttackPhase::Lunge && LungeArcHeight > 0.0f && Movement && Movement->IsMovingOnGround()
			&& GetTimeSeconds() - PhaseStartedAt > LungeDuration * 0.5f)
		{
			StopTimedMove();
			SetPhase(EDinoAttackPhase::Recovery, RecoveryDuration);
		}
	}
}

void UDinoLungeAttackComponent::OnPhaseTimerElapsed()
{
	switch (Phase)
	{
	case EDinoAttackPhase::Windup:
		BeginLunge();
		break;

	case EDinoAttackPhase::Lunge:
		// The pounce ran its full length without catching anyone.
		StopTimedMove();
		SetPhase(EDinoAttackPhase::Recovery, RecoveryDuration);
		break;

	case EDinoAttackPhase::Recovery:
		FinishAttack(EDinoAttackOutcome::Missed);
		break;

	default:
		Super::OnPhaseTimerElapsed();
		break;
	}
}

void UDinoLungeAttackComponent::BeginLunge()
{
	ADinoCreature* Creature = GetCreature();
	if (!Creature || !Creature->GetCharacterMovement())
	{
		FinishAttack(EDinoAttackOutcome::Cancelled);
		return;
	}

	// Locked now, at the end of the windup - this is what makes a sidestep work.
	LungeDirection = Creature->GetActorForwardVector().GetSafeNormal2D();

	// The engine's jump arc: a parabola peaking at Height halfway along. The upward part of the
	// root motion lifts the creature off the floor into falling by itself, and it lands through
	// the ordinary falling code. Same setup as Epic's AbilityTask_ApplyRootMotionJumpForce.
	TSharedPtr<FRootMotionSource_JumpForce> Pounce = MakeShared<FRootMotionSource_JumpForce>();
	Pounce->InstanceName = TEXT("DinoPounce");
	Pounce->Duration = LungeDuration;
	Pounce->Rotation = LungeDirection.Rotation();
	Pounce->Distance = LungeDistance;
	Pounce->Height = LungeArcHeight;
	ApplyRootMotion(Pounce);

	SetPhase(EDinoAttackPhase::Lunge, LungeDuration, true);
}

void UDinoLungeAttackComponent::ApplyTimedMove(const FVector& Target, float Duration, FName InstanceName)
{
	const ADinoCreature* Creature = GetCreature();
	if (!Creature)
	{
		return;
	}

	TSharedPtr<FRootMotionSource_MoveToForce> Move = MakeShared<FRootMotionSource_MoveToForce>();
	Move->InstanceName = InstanceName;
	Move->Duration = Duration;
	Move->StartLocation = Creature->GetActorLocation();
	Move->TargetLocation = Target;
	Move->bRestrictSpeedToExpected = false;
	ApplyRootMotion(Move);
}

void UDinoLungeAttackComponent::ApplyRootMotion(const TSharedPtr<FRootMotionSource>& Move)
{
	const ADinoCreature* Creature = GetCreature();
	UCharacterMovementComponent* Movement = Creature ? Creature->GetCharacterMovement() : nullptr;
	if (!Movement || !Move.IsValid())
	{
		return;
	}

	StopTimedMove();

	// Root motion rather than launching or setting velocity: an exact distance over an exact
	// time, swept through collision by the movement component so it cannot tunnel through a
	// wall, and replicated as ordinary movement.
	Move->AccumulateMode = ERootMotionAccumulateMode::Override;
	Move->Priority = 500;

	// The default, MaintainLastRootMotionVelocity, keeps full speed once the source ends, and the
	// creature slides on past the end of its move. Stop dead instead - in the air, that means it
	// simply drops the rest of the way under gravity.
	Move->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Move->FinishVelocityParams.SetVelocity = FVector::ZeroVector;

	TimedMoveRootMotionId = Movement->ApplyRootMotionSource(Move);
}

void UDinoLungeAttackComponent::StopTimedMove()
{
	if (TimedMoveRootMotionId == static_cast<uint16>(ERootMotionSourceID::Invalid))
	{
		return;
	}

	if (const ADinoCreature* Creature = GetCreature())
	{
		if (UCharacterMovementComponent* Movement = Creature->GetCharacterMovement())
		{
			Movement->RemoveRootMotionSourceByID(TimedMoveRootMotionId);
			Movement->StopMovementImmediately();
		}
	}

	TimedMoveRootMotionId = static_cast<uint16>(ERootMotionSourceID::Invalid);
}

FVector UDinoLungeAttackComponent::GetBiteCentre() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	return Owner->GetActorLocation()
		+ Owner->GetActorForwardVector().GetSafeNormal2D() * (GetCapsuleRadius() + BiteForwardOffset);
}

void UDinoLungeAttackComponent::TryBite()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Anyone in the path, not just the target: a second player standing between the raptor and
	// its victim gets bitten instead. That is the honest outcome of a committed lunge.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DinoBite), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, GetBiteCentre(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(BiteRadius), Params);

	ADinoCharacter* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		ADinoCharacter* Candidate = Cast<ADinoCharacter>(Overlap.GetActor());
		if (!Candidate || !IsValidVictim(Candidate))
		{
			continue;
		}

		const float Distance = FVector::DistSquared(GetBiteCentre(), Candidate->GetActorLocation());
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Candidate;
		}
	}

	if (Nearest)
	{
		LandBite(Nearest);
	}
}

void UDinoLungeAttackComponent::LandBite(ADinoCharacter* Victim)
{
	StopTimedMove();
	DealDamage(Victim, LungeHitDamage, LungeDirection);

	if (!Victim->IsAlive())
	{
		FinishAttack(EDinoAttackOutcome::Killed);
		return;
	}

	HoldVictim(Victim, EDinoRestraint::Pinned);
	SettleOverVictim(Victim);

	// No phase duration: the pin lasts until the victim dies or the raptor is staggered off.
	SetPhase(EDinoAttackPhase::Holding);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PinTimer, this, &UDinoLungeAttackComponent::ApplyPinDamage,
			PinDamageInterval, true);
	}
}

void UDinoLungeAttackComponent::SettleOverVictim(ADinoCharacter* Victim)
{
	ADinoCreature* Creature = GetCreature();
	UCapsuleComponent* Capsule = Creature ? Creature->GetCapsuleComponent() : nullptr;
	if (!Capsule || !Victim)
	{
		return;
	}

	// The two capsules have to overlap for the raptor to stand on the body. Only this raptor's
	// own movement ignores the victim, and only for the pin; the victim stays solid to everyone
	// else. If the victim is freed, the movement component pushes them apart again.
	Capsule->IgnoreActorWhenMoving(Victim, true);
	IgnoredVictim = Victim;

	// Facing the head - down the length of the body, the way it landed on them. The victim's feet
	// point back along the pin direction, toward where the raptor came from.
	const FVector TowardHead = -FVector(Victim->GetPinDirection()).GetSafeNormal2D();
	if (!TowardHead.IsNearlyZero())
	{
		Creature->SetActorRotation(FRotator(0.0f, TowardHead.Rotation().Yaw, 0.0f));
	}

	// The victim's spot is on the ground; the raptor's capsule centre goes its own half height
	// above that. Not its current height, which mid-pounce may be well off the floor.
	const FVector Target = Victim->GetPinnedCaptorSpot() + FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight());

	if (PinSettleDuration > 0.0f)
	{
		ApplyTimedMove(Target, PinSettleDuration, TEXT("DinoPinSettle"));
	}
	else
	{
		// Swept, so a wall between the bite and the body stops it rather than being passed through.
		Creature->SetActorLocation(Target, true);
	}
}

void UDinoLungeAttackComponent::StopIgnoringVictim()
{
	if (const ADinoCreature* Creature = GetCreature())
	{
		if (UCapsuleComponent* Capsule = Creature->GetCapsuleComponent())
		{
			// Also clears entries for a victim that has since been destroyed.
			Capsule->IgnoreActorWhenMoving(IgnoredVictim.Get(), false);
		}
	}
	IgnoredVictim.Reset();
}

void UDinoLungeAttackComponent::ApplyPinDamage()
{
	ADinoCharacter* Victim = HeldVictim;

	// Gone (disconnected) or already dead from something else.
	if (!IsValid(Victim) || !Victim->IsAlive())
	{
		FinishAttack(IsValid(Victim) ? EDinoAttackOutcome::Killed : EDinoAttackOutcome::Cancelled);
		return;
	}

	DealDamage(Victim, PinDamagePerTick, LungeDirection);

	if (!Victim->IsAlive())
	{
		FinishAttack(EDinoAttackOutcome::Killed);
	}
}

void UDinoLungeAttackComponent::CleanUpAttack()
{
	StopTimedMove();
	StopIgnoringVictim();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PinTimer);
	}
}

void UDinoLungeAttackComponent::DrawDebug(const UWorld* World, float Lifetime) const
{
	Super::DrawDebug(World, Lifetime);

	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	if (Phase == EDinoAttackPhase::Windup)
	{
		// Where the lunge will go if it launched this instant. Watching this swing during the
		// windup is watching the dodge window.
		const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
		const FVector Landing = GetBiteCentre() + Forward * LungeDistance;
		DrawDebugLine(World, Owner->GetActorLocation(), Landing, FColor::Yellow, false, Lifetime, 0, 3.0f);
		DrawDebugSphere(World, Landing, BiteRadius, 12, FColor::Yellow, false, Lifetime, 0, 1.5f);
	}
	else if (Phase == EDinoAttackPhase::Lunge)
	{
		DrawDebugSphere(World, GetBiteCentre(), BiteRadius, 12, FColor::Red, false, Lifetime, 0, 3.0f);
	}
}

void UDinoLungeAttackComponent::AppendSetupCheck(TArray<FString>& OutLines) const
{
	Super::AppendSetupCheck(OutLines);

	OutLines.Add(CheckLine(AttackTriggerRange <= GetMaxReach(), TEXT("lunge trigger range within reach"),
		FString::Printf(TEXT("trigger %.0f, reach %.0f"), AttackTriggerRange, GetMaxReach())));

	OutLines.Add(CheckLine(WindupMovementFrames == 0, TEXT("holds still during windup"),
		WindupMovementFrames == 0
			? FString()
			: FString::Printf(TEXT("moved in %d frames - does the StateTree have an Attacking state?"),
				WindupMovementFrames)));
}
