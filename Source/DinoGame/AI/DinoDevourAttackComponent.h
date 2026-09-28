#pragma once

#include "CoreMinimal.h"
#include "AI/DinoAttackComponent.h"
#include "DinoDevourAttackComponent.generated.h"

/**
 * T-Rex attack: step into the mouth zone and you are eaten. No windup, no dodge, no rescue.
 *
 * The T-Rex is meant to be a hazard to avoid rather than a fight, so there is deliberately no
 * counterplay once it has you - the counterplay is not being in front of it. The victim is held
 * for DevourDuration before dying, so they can see what is happening to them; that window is
 * where a first-person-in-the-jaws view will go (see ADinoCharacter::OnRestraintBegan).
 *
 * Triggers from any awareness, not only Hunting. The zone is small and frontal, and walking
 * into a T-Rex's mouth should get you eaten whether or not it had noticed you yet.
 *
 * Unstoppable by construction: CanBeInterrupted refuses every stagger while this component is
 * busy, independent of the creature's vulnerability settings.
 */
UCLASS(ClassGroup = (Dino), meta = (BlueprintSpawnableComponent, DisplayName = "Dino Devour Attack"))
class DINOGAME_API UDinoDevourAttackComponent : public UDinoAttackComponent
{
	GENERATED_BODY()

public:
	UDinoDevourAttackComponent();

	virtual void DrawDebug(const UWorld* World, float Lifetime) const override;
	virtual void AppendSetupCheck(TArray<FString>& OutLines) const override;

protected:
	/**
	 * How far past the front of its capsule the T-Rex can grab, in world units, measured to the
	 * surface of the victim's capsule.
	 *
	 * Must sit comfortably above the Hunting MoveTo's Acceptable Radius in ST_TRex. If the move
	 * finishes first, the T-Rex stops short of the zone and stands in front of you instead.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float GrabRange = 350.0f;

	/**
	 * Half-width of the mouth zone, in degrees either side of the body's facing.
	 *
	 * The body, not the eyes: the eyes track the target independently while hunting, but it is
	 * the jaws that have to be pointing at you.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float GrabHalfAngle = 35.0f;

	/**
	 * Seconds the victim is held before dying. Long enough to understand what happened, short
	 * enough not to become boring - and the window a jaw camera will play out in.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float DevourDuration = 3.0f;

	/**
	 * Seconds the T-Rex stands still after a kill. In co-op this is the other players' window to
	 * get out of sight before it turns on them.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float SwallowDuration = 2.0f;

	virtual bool CanTriggerFromAwareness(EDinoAwareness Awareness) const override;
	virtual ADinoCharacter* SelectVictim(bool bForced) const override;
	virtual void BeginAttack(ADinoCharacter* Victim) override;
	virtual void OnPhaseTimerElapsed() override;
	virtual bool CanBeInterrupted() const override { return false; }

private:
	bool IsInMouthZone(const ADinoCharacter* Victim) const;

	/** Whether the victim actually died, for the outcome reported after the swallow. */
	bool bKilledVictim = false;
};
