#pragma once

#include "CoreMinimal.h"
#include "AI/DinoAttackComponent.h"
#include "DinoLungeAttackComponent.generated.h"

/**
 * Raptor attack: telegraph, dash, bite, pin.
 *
 * Windup - stops and turns to face the target. The dodge window.
 * Lunge  - dashes a fixed distance along its facing, biting the first valid player it reaches.
 * Hit    - the victim is pinned and takes damage over time until an ally staggers the raptor.
 * Miss   - a recovery pause, then a cooldown. The punish window.
 *
 * Dodgeable because the lunge commits to the raptor's *facing* at the end of the windup, not
 * to the target: sidestep during the windup and it lunges where you were. That makes the
 * creature's turn rate the difficulty dial - TurnRateDegreesPerSecond on the creature.
 *
 * The windup, the lunge and the pin can all be interrupted by a stagger, if the creature's
 * vulnerability settings allow staggering at all.
 */
UCLASS(ClassGroup = (Dino), meta = (BlueprintSpawnableComponent, DisplayName = "Dino Lunge Attack"))
class DINOGAME_API UDinoLungeAttackComponent : public UDinoAttackComponent
{
	GENERATED_BODY()

public:
	virtual void DrawDebug(const UWorld* World, float Lifetime) const override;
	virtual void AppendSetupCheck(TArray<FString>& OutLines) const override;

	/**
	 * Furthest the bite can land past the front of the capsule: the dash plus the bite sphere.
	 * The trigger range has to sit inside this, or the raptor lunges from too far and always
	 * falls short.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|Attack")
	float GetMaxReach() const { return LungeDistance + BiteForwardOffset + BiteRadius; }

protected:
	/** How close the target must be, capsule surface to capsule surface, before it lunges. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float AttackTriggerRange = 700.0f;

	/** The telegraph. Longer is fairer; shorter is scarier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float WindupDuration = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float LungeDistance = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.05"))
	float LungeDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "1.0"))
	float BiteRadius = 150.0f;

	/**
	 * Distance from the front surface of the capsule to the centre of the bite sphere.
	 *
	 * Measured from the capsule surface rather than the actor's centre so it scales with the
	 * creature: the same value suits a raptor and a T-Rex-sized stand-in.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float BiteForwardOffset = 100.0f;

	/** Damage from the bite that lands the pin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float LungeHitDamage = 15.0f;

	/** Damage each pin tick. With the defaults, a full-health player lasts about five seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float PinDamagePerTick = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.05"))
	float PinDamageInterval = 0.5f;

	/** Pause after a miss before it can act again. The window to get away. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float RecoveryDuration = 0.8f;

	/**
	 * Seconds to climb from where the bite landed onto the pinned victim. 0 snaps straight there.
	 * Where it stands is the victim's call - PinnedCaptorOffset on the player.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float PinSettleDuration = 0.2f;

	virtual bool CanTriggerFromAwareness(EDinoAwareness Awareness) const override;
	virtual ADinoCharacter* SelectVictim(bool bForced) const override;

	/** Forced only: lets DinoSetState loop lunges with nobody to aim at, straight ahead. */
	virtual bool CanAttackWithoutVictim(bool bForced) const override { return bForced; }

	virtual void BeginAttack(ADinoCharacter* Victim) override;
	virtual void OnPhaseTimerElapsed() override;
	virtual void TickPhase(float DeltaTime) override;
	virtual void CleanUpAttack() override;

private:
	void BeginLunge();
	void TryBite();
	void LandBite(ADinoCharacter* Victim);
	void ApplyPinDamage();
	FVector GetBiteCentre() const;

	/** Stands over the pinned victim, facing their head. */
	void SettleOverVictim(ADinoCharacter* Victim);

	/** Lets the capsules overlap again. Safe to call when not settled. */
	void StopIgnoringVictim();

	/** A timed, swept move to Target - see BeginLunge for why root motion. Replaces any current one. */
	void ApplyTimedMove(const FVector& Target, float Duration, FName InstanceName);
	void StopTimedMove();

	/**
	 * The current timed move's root motion source - the dash, or the climb onto the victim - so a
	 * stagger can remove it mid-flight. 0 = none.
	 */
	uint16 TimedMoveRootMotionId = 0;

	/**
	 * The victim this raptor's capsule is passing through. Ignored by this raptor's movement only:
	 * every other player still collides with it, and flares, which are not pawns, still hit it.
	 */
	TWeakObjectPtr<AActor> IgnoredVictim;

	FVector LungeDirection = FVector::ForwardVector;
	FTimerHandle PinTimer;

	/**
	 * Frames in which the creature moved during a windup.
	 *
	 * The windup should be stationary. Movement here almost always means the StateTree has no
	 * Attacking state, so the Hunting MoveTo is still driving the pawn - which reads in game as
	 * "the telegraph slides toward me". Reported by DinoAICheck.
	 */
	int32 WindupMovementFrames = 0;
};
