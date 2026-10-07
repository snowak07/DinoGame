#include "AI/DinoIdleComponent.h"

#include "AIController.h"
#include "AI/DinoAIControllerBase.h"
#include "AI/DinoAttackComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	/** Walks that fail to start before it stops insisting on home. */
	constexpr int32 FailedWalksBeforeWanderingLocally = 3;

	/** A leg that cannot start waits this long before trying again, rather than retrying every frame. */
	constexpr float RetryPauseSeconds = 1.0f;
}

UDinoIdleComponent::UDinoIdleComponent()
{
	// Ticked by the StateTree task instead, so it only ever runs while the creature is idle.
	PrimaryComponentTick.bCanEverTick = false;
}

AAIController* UDinoIdleComponent::GetAIController() const
{
	return Cast<AAIController>(GetOwner());
}

APawn* UDinoIdleComponent::GetPawn() const
{
	const AAIController* AI = GetAIController();
	return AI ? AI->GetPawn() : nullptr;
}

ANavigationData* UDinoIdleComponent::GetNavData() const
{
	const ADinoAIControllerBase* AI = Cast<ADinoAIControllerBase>(GetOwner());
	return AI ? AI->GetCreatureNavData() : nullptr;
}

double UDinoIdleComponent::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UDinoIdleComponent::BeginIdle()
{
	const APawn* Pawn = GetPawn();
	if (!Pawn || IsIdling())
	{
		return;
	}

	// The first time only. A creature that later idles after a hunt keeps its original home and
	// drifts back to it.
	if (!bHasHome)
	{
		Home = Pawn->GetActorLocation();
		bHasHome = true;
	}

	FailedWalks = 0;
	SetIdleSpeed(true);
	StartPause();
}

void UDinoIdleComponent::EndIdle()
{
	if (!IsIdling())
	{
		return;
	}

	Phase = EPhase::Inactive;
	SetIdleSpeed(false);

	// Only stops path following. Whatever state comes next issues its own move straight after.
	if (AAIController* AI = GetAIController())
	{
		AI->StopMovement();
	}
}

void UDinoIdleComponent::TickIdle(float DeltaTime)
{
	APawn* Pawn = GetPawn();
	const AAIController* AI = GetAIController();
	if (!Pawn || !AI || !IsIdling())
	{
		return;
	}

	if (Phase == EPhase::Walking)
	{
		// Polled rather than waiting on OnMoveCompleted, which the controller already overrides
		// for the chase readout. Idle covers arriving, being blocked, and losing the path alike:
		// whichever it was, the stroll is over and it stops to look around.
		const bool bMoveOver = AI->GetMoveStatus() == EPathFollowingStatus::Idle;
		if (bMoveOver || Now() >= WalkGivesUpAt)
		{
			StartPause();
		}
		return;
	}

	// Pausing: turn toward the current glance, choosing a new one when this one has been held.
	if (Now() >= PauseEndsAt)
	{
		StartWalk();
		return;
	}

	if (Now() >= NextGlanceAt)
	{
		// Relative to the way it stopped, not the way it is facing now, so a run of glances
		// sweeps back and forth across one view rather than turning it steadily around.
		GlanceYaw = PauseBaseYaw + FMath::FRandRange(-LookAroundAngle, LookAroundAngle);
		NextGlanceAt = Now() + FMath::FRandRange(GlanceMinSeconds, FMath::Max(GlanceMinSeconds, GlanceMaxSeconds));
	}

	// Turned by hand, as the lunge windup does: bOrientRotationToMovement only turns a creature
	// that is moving. With no focus set, the eyes follow the body.
	const FRotator Current = Pawn->GetActorRotation();
	const FRotator Wanted(0.0f, GlanceYaw, 0.0f);
	Pawn->SetActorRotation(FMath::RInterpConstantTo(Current, Wanted, DeltaTime, GlanceTurnRate));
}

void UDinoIdleComponent::StartPause()
{
	const APawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Phase = EPhase::Pausing;

	const float Duration = FMath::FRandRange(PauseMinSeconds, FMath::Max(PauseMinSeconds, PauseMaxSeconds));
	PauseEndsAt = Now() + Duration;

	// The first look is straight ahead, held for a moment, so it does not snap into a turn the
	// instant it stops.
	PauseBaseYaw = Pawn->GetActorRotation().Yaw;
	GlanceYaw = PauseBaseYaw;
	NextGlanceAt = Now() + FMath::FRandRange(GlanceMinSeconds, FMath::Max(GlanceMinSeconds, GlanceMaxSeconds));
}

void UDinoIdleComponent::StartWalk()
{
	AAIController* AI = GetAIController();
	const APawn* Pawn = GetPawn();
	if (!AI || !Pawn)
	{
		return;
	}

	FVector Picked;
	const EPathFollowingRequestResult::Type Result = PickDestination(Picked)
		? AI->MoveToLocation(Picked, AcceptanceRadius, /*bStopOnOverlap*/ true, /*bUsePathfinding*/ true,
			/*bProjectDestinationToNavigation*/ false, /*bCanStrafe*/ false)
		: EPathFollowingRequestResult::Failed;

	if (Result != EPathFollowingRequestResult::RequestSuccessful)
	{
		// No point found, or none it can path to. Wait a moment and try again, rather than
		// standing frozen or retrying every frame.
		++FailedWalks;
		Phase = EPhase::Pausing;
		PauseEndsAt = Now() + RetryPauseSeconds;
		NextGlanceAt = PauseEndsAt;
		return;
	}

	FailedWalks = 0;
	Destination = Picked;
	Phase = EPhase::Walking;

	// A generous allowance for the walk at idle speed. A leg that takes longer than this is
	// stuck somewhere path following has not noticed, and gets abandoned.
	const UCharacterMovementComponent* Movement = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	const float Speed = Movement ? FMath::Max(Movement->GetMaxSpeed(), 50.0f) : 200.0f;
	WalkGivesUpAt = Now() + FVector::Dist2D(Pawn->GetActorLocation(), Picked) / Speed * 2.0f + 3.0f;
}

bool UDinoIdleComponent::PickDestination(FVector& OutDestination) const
{
	const APawn* Pawn = GetPawn();
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!Pawn || !Nav)
	{
		return false;
	}

	const FVector Here = Pawn->GetActorLocation();
	const FVector Facing = Pawn->GetActorForwardVector().GetSafeNormal2D();
	ANavigationData* NavData = GetNavData();

	// Home normally. Somewhere it cannot path from - home off the navmesh, or cut off by a
	// level change - and every walk fails, so after a few it wanders where it stands instead.
	const bool bFromHome = bStayNearHome && bHasHome && FailedWalks < FailedWalksBeforeWanderingLocally;
	FVector Origin = bFromHome ? Home : Here;

	// Placed and spawned locations are the capsule centre, above the floor. Projected first so
	// a tall creature's home is still found on the navmesh beneath it.
	FNavLocation Projected;
	if (Nav->ProjectPointToNavigation(Origin, Projected, FVector(300.0f, 300.0f, 1000.0f), NavData))
	{
		Origin = Projected.Location;
	}

	bool bFound = false;
	float BestLength = -1.0f;

	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		FNavLocation Candidate;
		if (!Nav->GetRandomReachablePointInRadius(Origin, WanderRadius, Candidate, NavData))
		{
			continue;
		}

		const FVector Leg = Candidate.Location - Here;
		const float Length = Leg.Size2D();

		// Long enough, and not doubling back - anything up to about 100 degrees off its facing.
		// Its facing after a pause is wherever it last looked, so this also makes it tend to set
		// off toward whatever it was just looking at.
		if (Length >= MinLegDistance && FVector::DotProduct(Leg.GetSafeNormal2D(), Facing) > -0.2f)
		{
			OutDestination = Candidate.Location;
			return true;
		}

		if (Length > BestLength)
		{
			BestLength = Length;
			OutDestination = Candidate.Location;
			bFound = true;
		}
	}

	return bFound;
}

void UDinoIdleComponent::SetIdleSpeed(bool bIdle)
{
	const ACharacter* Character = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	if (bIdle && SavedMaxWalkSpeed < 0.0f)
	{
		SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
		Movement->MaxWalkSpeed = SavedMaxWalkSpeed * WalkSpeedMultiplier;
	}
	else if (!bIdle && SavedMaxWalkSpeed >= 0.0f)
	{
		Movement->MaxWalkSpeed = SavedMaxWalkSpeed;
		SavedMaxWalkSpeed = -1.0f;
	}
}

FString UDinoIdleComponent::DescribeIdle() const
{
	const APawn* Pawn = GetPawn();
	switch (Phase)
	{
	case EPhase::Walking:
		return FString::Printf(TEXT("idle: strolling, %.0fm to go"),
			Pawn ? FVector::Dist2D(Pawn->GetActorLocation(), Destination) / 100.0f : 0.0f);

	case EPhase::Pausing:
		return FString::Printf(TEXT("idle: looking around, %.1fs%s"),
			FMath::Max(0.0, PauseEndsAt - Now()),
			FailedWalks > 0 ? *FString::Printf(TEXT(" (%d walks failed to start)"), FailedWalks) : TEXT(""));

	default:
		return TEXT("idle: not running - is the Dino Idle task in the Idle state?");
	}
}

void UDinoIdleComponent::DrawDebug(const UWorld* World, float Lifetime) const
{
	const APawn* Pawn = GetPawn();
	if (!World || !Pawn || !bHasHome)
	{
		return;
	}

	const FColor HomeColour(150, 150, 150);
	DrawDebugSphere(World, Home, 40.0f, 8, HomeColour, false, Lifetime, 0, 1.5f);
	if (bStayNearHome)
	{
		DrawDebugCircle(World, Home, WanderRadius, 48, HomeColour, false, Lifetime, 0, 1.5f,
			FVector::ForwardVector, FVector::RightVector, false);
	}

	if (Phase == EPhase::Walking)
	{
		DrawDebugSphere(World, Destination, 60.0f, 10, HomeColour, false, Lifetime, 0, 2.5f);
		DrawDebugLine(World, Pawn->GetActorLocation(), Destination, HomeColour, false, Lifetime, 0, 1.5f);
	}
}

void UDinoIdleComponent::AppendSetupCheck(TArray<FString>& OutLines) const
{
	const APawn* Pawn = GetPawn();
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!Pawn || !Nav)
	{
		return;
	}

	// The question idle actually asks: is there anywhere to stroll to from home? A creature
	// placed outside the navmesh bounds stands still all game, which looks like idle being broken.
	const FVector Origin = bHasHome ? Home : Pawn->GetActorLocation();
	FNavLocation Projected;
	FNavLocation Found;
	ANavigationData* NavData = GetNavData();
	const bool bOnNav = Nav->ProjectPointToNavigation(Origin, Projected, FVector(300.0f, 300.0f, 1000.0f), NavData);
	const bool bCanWander = bOnNav && Nav->GetRandomReachablePointInRadius(Projected.Location, WanderRadius, Found, NavData);

	OutLines.Add(UDinoAttackComponent::CheckLine(bCanWander, TEXT("idle can wander from home"),
		bCanWander
			? FString::Printf(TEXT("radius %.0fm"), WanderRadius / 100.0f)
			: FString(TEXT("home is not on the navmesh - extend the NavMeshBoundsVolume, or move the creature"))));

	OutLines.Add(UDinoAttackComponent::CheckLine(PauseMaxSeconds >= PauseMinSeconds && GlanceMaxSeconds >= GlanceMinSeconds,
		TEXT("idle min/max ranges in order"), TEXT("")));
}
