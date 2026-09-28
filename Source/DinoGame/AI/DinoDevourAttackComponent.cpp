#include "AI/DinoDevourAttackComponent.h"

#include "AI/DinoCreature.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

UDinoDevourAttackComponent::UDinoDevourAttackComponent()
{
	// The swallow already keeps it still after a kill; a long cooldown on top would only let
	// someone stand in its mouth with impunity.
	AttackCooldown = 0.5f;
}

bool UDinoDevourAttackComponent::CanTriggerFromAwareness(EDinoAwareness Awareness) const
{
	// Anything but already attacking. See the class comment for why not just Hunting.
	return Awareness != EDinoAwareness::Attacking;
}

ADinoCharacter* UDinoDevourAttackComponent::SelectVictim(bool /*bForced*/) const
{
	// Every player, not just the controller's target: whoever is in the mouth gets eaten,
	// including someone the T-Rex had not noticed. Players are few, so walking the list is
	// cheaper than it sounds and far simpler than an overlap query.
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const AActor* Owner = GetOwner();
	if (!GameState || !Owner)
	{
		return nullptr;
	}

	ADinoCharacter* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		ADinoCharacter* Candidate = PlayerState ? Cast<ADinoCharacter>(PlayerState->GetPawn()) : nullptr;
		if (!Candidate || !IsInMouthZone(Candidate) || !IsValidVictim(Candidate))
		{
			continue;
		}

		const float Distance = FVector::DistSquared(Owner->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Candidate;
		}
	}

	return Nearest;
}

bool UDinoDevourAttackComponent::IsInMouthZone(const ADinoCharacter* Victim) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Victim)
	{
		return false;
	}

	const FVector ToVictim = Victim->GetActorLocation() - Owner->GetActorLocation();

	// Roughly the same ground. Without this a player on a ledge above the T-Rex's head, or in a
	// trench below its feet, would count as "in front" from the horizontal test alone.
	const float VerticalReach = Owner->GetSimpleCollisionHalfHeight() + Victim->GetSimpleCollisionHalfHeight();
	if (FMath::Abs(ToVictim.Z) > VerticalReach)
	{
		return false;
	}

	if (EdgeDistance2D(Victim) > GrabRange)
	{
		return false;
	}

	// Standing inside the T-Rex's own footprint has no meaningful direction; treat it as in front.
	const FVector Flat = ToVictim.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		return true;
	}

	const FVector Facing = Owner->GetActorForwardVector().GetSafeNormal2D();
	return FVector::DotProduct(Facing, Flat) >= FMath::Cos(FMath::DegreesToRadians(GrabHalfAngle));
}

void UDinoDevourAttackComponent::BeginAttack(ADinoCharacter* Victim)
{
	bKilledVictim = false;

	HoldVictim(Victim, EDinoRestraint::Devoured);
	SetPhase(EDinoAttackPhase::Holding, DevourDuration);
}

void UDinoDevourAttackComponent::OnPhaseTimerElapsed()
{
	switch (Phase)
	{
	case EDinoAttackPhase::Holding:
	{
		ADinoCharacter* Victim = HeldVictim;
		if (IsValid(Victim) && Victim->IsAlive())
		{
			// Remaining health plus a margin, through the ordinary damage path, so death happens
			// in the one place it is handled rather than being special-cased here. If damage
			// mitigation is ever added, a devour has to bypass it - this is meant to be certain.
			const FVector Direction = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
			DealDamage(Victim, Victim->GetCurrentHealth() + 1.0f, Direction);
			bKilledVictim = !Victim->IsAlive();
		}

		ReleaseVictim();
		SetPhase(EDinoAttackPhase::Recovery, SwallowDuration);
		break;
	}

	case EDinoAttackPhase::Recovery:
		// Cancelled rather than Killed when the victim was gone before the bite - disconnected
		// mid-devour - so the log does not claim a kill that never happened.
		FinishAttack(bKilledVictim ? EDinoAttackOutcome::Killed : EDinoAttackOutcome::Cancelled);
		break;

	default:
		Super::OnPhaseTimerElapsed();
		break;
	}
}

void UDinoDevourAttackComponent::DrawDebug(const UWorld* World, float Lifetime) const
{
	Super::DrawDebug(World, Lifetime);

	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	// Drawn near the ground, where the players are, rather than at the capsule centre high above.
	const FVector Origin = Owner->GetActorLocation()
		- FVector(0.0f, 0.0f, Owner->GetSimpleCollisionHalfHeight() - 20.0f);
	const FVector Facing = Owner->GetActorForwardVector().GetSafeNormal2D();

	// The drawn reach adds a typical player radius, so the arc marks where a player's *centre*
	// gets eaten - the edge a player actually has to stay behind.
	const float Reach = GetCapsuleRadius() + GrabRange + 34.0f;
	const float HalfAngleRad = FMath::DegreesToRadians(GrabHalfAngle);

	const FColor Colour = IsBusy() ? FColor::Red : FColor(255, 120, 0);

	DrawDebugLine(World, Origin, Origin + Facing.RotateAngleAxis(GrabHalfAngle, FVector::UpVector) * Reach,
		Colour, false, Lifetime, 0, 3.0f);
	DrawDebugLine(World, Origin, Origin + Facing.RotateAngleAxis(-GrabHalfAngle, FVector::UpVector) * Reach,
		Colour, false, Lifetime, 0, 3.0f);
	DrawDebugCircleArc(World, Origin, Reach, Facing, HalfAngleRad, 16, Colour, false, Lifetime, 0, 3.0f);
}

void UDinoDevourAttackComponent::AppendSetupCheck(TArray<FString>& OutLines) const
{
	Super::AppendSetupCheck(OutLines);

	// Printed rather than validated: the MoveTo radius it has to beat lives in the StateTree
	// asset, which nothing in C++ can read.
	OutLines.Add(CheckLine(true, TEXT("devour zone"), FString::Printf(
		TEXT("%.0f uu, +/-%.0f deg - ST_TRex Hunting MoveTo Acceptable Radius must be below %.0f"),
		GrabRange, GrabHalfAngle, GrabRange)));

	OutLines.Add(CheckLine(true, TEXT("devour is unstoppable"), TEXT("staggers are refused while holding")));
}
