#include "AI/DinoLungeAttackComponent.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoCreature.h"
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
		// The dash ran its full length without catching anyone.
		StopLunge();
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
	UCharacterMovementComponent* Movement = Creature ? Creature->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		FinishAttack(EDinoAttackOutcome::Cancelled);
		return;
	}

	// Locked now, at the end of the windup - this is what makes a sidestep work.
	LungeDirection = Creature->GetActorForwardVector().GetSafeNormal2D();
	const FVector Start = Creature->GetActorLocation();

	// Root motion rather than launching or setting velocity: an exact distance over an exact
	// time, swept through collision by the movement component so it cannot tunnel through a
	// wall, and replicated as ordinary movement. Same setup as Epic's
	// AbilityTask_ApplyRootMotionMoveToForce.
	TSharedPtr<FRootMotionSource_MoveToForce> Dash = MakeShared<FRootMotionSource_MoveToForce>();
	Dash->InstanceName = TEXT("DinoLunge");
	Dash->AccumulateMode = ERootMotionAccumulateMode::Override;
	Dash->Priority = 500;
	Dash->Duration = LungeDuration;
	Dash->StartLocation = Start;
	Dash->TargetLocation = Start + LungeDirection * LungeDistance;
	Dash->bRestrictSpeedToExpected = false;

	// The default, MaintainLastRootMotionVelocity, keeps full dash speed once the source ends,
	// and the creature slides on past the end of its lunge. Stop dead instead.
	Dash->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Dash->FinishVelocityParams.SetVelocity = FVector::ZeroVector;

	LungeRootMotionId = Movement->ApplyRootMotionSource(Dash);

	SetPhase(EDinoAttackPhase::Lunge, LungeDuration, true);
}

void UDinoLungeAttackComponent::StopLunge()
{
	if (LungeRootMotionId == static_cast<uint16>(ERootMotionSourceID::Invalid))
	{
		return;
	}

	if (const ADinoCreature* Creature = GetCreature())
	{
		if (UCharacterMovementComponent* Movement = Creature->GetCharacterMovement())
		{
			Movement->RemoveRootMotionSourceByID(LungeRootMotionId);
			Movement->StopMovementImmediately();
		}
	}

	LungeRootMotionId = static_cast<uint16>(ERootMotionSourceID::Invalid);
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
	StopLunge();
	DealDamage(Victim, LungeHitDamage, LungeDirection);

	if (!Victim->IsAlive())
	{
		FinishAttack(EDinoAttackOutcome::Killed);
		return;
	}

	HoldVictim(Victim, EDinoRestraint::Pinned);

	// No phase duration: the pin lasts until the victim dies or the raptor is staggered off.
	SetPhase(EDinoAttackPhase::Holding);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PinTimer, this, &UDinoLungeAttackComponent::ApplyPinDamage,
			PinDamageInterval, true);
	}
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
	StopLunge();

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
