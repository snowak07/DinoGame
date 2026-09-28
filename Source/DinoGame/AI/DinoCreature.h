#pragma once

#include "CoreMinimal.h"
#include "AI/DinoAITypes.h"
#include "GameFramework/Character.h"
#include "DinoCreature.generated.h"

class UDinoAttackComponent;
class UNavigationInvokerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDinoAwarenessChanged, EDinoAwareness, NewAwareness, EDinoAwareness, OldAwareness);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDinoCreatureDamaged, float, Damage, AActor*, DamageCauser);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDinoCreatureDied);

/**
 * Base pawn for every AI creature, from the T-Rex down to ambient birds.
 *
 * Deliberately holds no behaviour. Behaviour lives in ADinoAIControllerBase plus a
 * per-species StateTree; this class is the replicated, client-visible half — the part
 * that has to exist on every machine because the AI controller does not.
 *
 * Species differences are meant to be data and components on a Blueprint child, not
 * subclasses of this. See the "general skills a dinosaur can give" design note.
 *
 * Not Abstract, for the same reason as ADinoAIControllerBase: a shared StateTree's schema
 * names this as its Context Actor Class, and editor class pickers hide abstract classes.
 */
UCLASS()
class DINOGAME_API ADinoCreature : public ACharacter
{
	GENERATED_BODY()

public:
	ADinoCreature();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	EDinoAwareness GetAwareness() const { return Awareness; }

	/**
	 * Server only. Ignored elsewhere, rather than asserting: a Blueprint calling this on
	 * a client is a mistake worth surviving, not crashing a playtest over.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dino|AI")
	void SetAwareness(EDinoAwareness NewAwareness);

	/** Fires on every machine, including the server, whenever awareness changes. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|AI")
	FDinoAwarenessChanged OnAwarenessChanged;

	/** Display name used in debug output. Falls back to the class name when empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI")
	FName SpeciesName;

	/**
	 * How fast the creature yaws toward its direction of travel, in degrees per second.
	 *
	 * A gameplay dial, not just a cosmetic one: a large predator that turns slowly can be
	 * outmanoeuvred, which is most of what makes an unfightable one survivable. Raise it
	 * for small agile creatures, lower it for heavy ones.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Movement", meta = (ClampMin = "1.0"))
	float TurnRateDegreesPerSecond = 180.0f;

	/**
	 * Height of the creature's eyes above its feet, in world units. Zero keeps the engine default.
	 *
	 * This is where the creature sees from: AI sight traces run from here, so it decides what
	 * cover works. On a T-Rex the engine default is badly wrong - APawn::BaseEyeHeight is 64
	 * above the *capsule centre*, a human value, which on a 400-tall capsule puts its eyes at
	 * belly height and lets it be hidden from by waist-high props.
	 *
	 * Measured from the feet rather than from the actor location because that is how the model
	 * reads and how an artist would quote it; the capsule offset is applied at runtime, so
	 * resizing the capsule does not silently move the eyes.
	 *
	 * BaseEyeHeight is deliberately left alone. ACharacter recomputes it on every crouch, and
	 * APawn::GetNavAgentLocation subtracts it to find the pawn's position on the navmesh - so
	 * repurposing it as a dinosaur eye height would quietly move the creature's navigation
	 * origin hundreds of units off the ground.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI", meta = (ClampMin = "0.0"))
	float EyeHeightAboveFeet = 0.0f;

	/** Where this creature sees from. Overridden so AI perception uses EyeHeightAboveFeet. */
	virtual FVector GetPawnViewLocation() const override;

	/**
	 * The value to type into an EQS Trace test's Context Height Offset.
	 *
	 * That field is an offset from the querier's actor location - the capsule centre - while
	 * eye height here is measured from the feet, so the two are not the same number. Exposed
	 * rather than left as arithmetic because a query whose traces start somewhere other than
	 * the creature's eyes disagrees with perception about what is visible, and that disagreement
	 * is invisible in game: the creature simply searches places it can plainly see.
	 *
	 * Reported by DinoAICheck so the number can be read off rather than derived.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	float GetEQSContextHeightOffset() const;

	/** Actual eye height above the feet, whether configured here or inherited from the engine. */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	float GetEyeHeightAboveFeet() const;

	// --- Combat -------------------------------------------------------------------------------

	/** This species' attack, or null for a creature that never attacks. */
	UFUNCTION(BlueprintPure, Category = "Dino|Attack")
	UDinoAttackComponent* GetAttackComponent() const;

	/**
	 * Decides what a hit does to this species: stagger, lose health, both, or nothing.
	 *
	 * The single entry point for hurting a creature. A future player weapon calls
	 * ApplyDamage and lands here exactly as DinoHitDino does today.
	 */
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Always false for a creature that cannot be killed. */
	UFUNCTION(BlueprintPure, Category = "Dino|Vulnerability")
	bool IsDead() const { return bCanBeKilled && CurrentHealth <= 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Dino|Vulnerability")
	float GetCurrentHealth() const { return CurrentHealth; }

	/** One line for DinoAICheck and DinoHitDino, e.g. "stagger >= 10 for 1.2s, killable 90/150". */
	FString DescribeVulnerability() const;

	/**
	 * Server only, every hit including ones this species ignores - so a T-Rex can still roar
	 * when shot, without the hit doing anything.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Vulnerability")
	FDinoCreatureDamaged OnCreatureDamaged;

	/** Every machine. The Blueprint hook for a ragdoll or death animation. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Vulnerability")
	FDinoCreatureDied OnCreatureDied;

	/**
	 * Whether a hard enough hit knocks this creature off its attack.
	 *
	 * The rescue mechanic: staggering a raptor releases the player it has pinned. Off by default,
	 * and off on the T-Rex - an unconfigured creature should be invulnerable rather than
	 * accidentally interruptible.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Vulnerability")
	bool bCanBeStaggered = false;

	/** Smallest single hit that staggers. Chip damage below this is ignored for stagger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Vulnerability", meta = (ClampMin = "0.0", EditCondition = "bCanBeStaggered"))
	float StaggerDamageThreshold = 10.0f;

	/** Seconds a stagger holds the creature still before it can act again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Vulnerability", meta = (ClampMin = "0.1", EditCondition = "bCanBeStaggered"))
	float StaggerDuration = 1.2f;

	/**
	 * Whether this species can die at all.
	 *
	 * Off by default. Left as an option so some species can be killable without the rest of
	 * the code assuming either way: the T-Rex never, raptors to be decided.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Vulnerability")
	bool bCanBeKilled = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Vulnerability", meta = (ClampMin = "1.0", EditCondition = "bCanBeKilled"))
	float MaxHealth = 150.0f;

protected:
	virtual void BeginPlay() override;

	/**
	 * Generates navmesh around this creature.
	 *
	 * Required because navigation is configured to build only around invokers. Without
	 * it a creature outside any player's radius stands on no navmesh and cannot move —
	 * which presents as an AI bug rather than a navigation one.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|AI")
	TObjectPtr<UNavigationInvokerComponent> NavigationInvoker;

	UPROPERTY(ReplicatedUsing = OnRep_Awareness, BlueprintReadOnly, Category = "Dino|AI")
	EDinoAwareness Awareness = EDinoAwareness::Unaware;

	UFUNCTION()
	void OnRep_Awareness(EDinoAwareness OldAwareness);

	/** Replicated so clients agree on whether the creature is dead. Unused unless killable. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, BlueprintReadOnly, Category = "Dino|Vulnerability")
	float CurrentHealth = 150.0f;

	UFUNCTION()
	void OnRep_CurrentHealth(float OldHealth);

private:
	void BroadcastAwarenessChange(EDinoAwareness OldAwareness);

	/** Runs on every machine, from both the server's write and the RepNotify. */
	void ApplyHealthChange(float OldHealth);

	/** Mirrors ADinoCharacter::HandleDeath: stop, stop blocking, and hand over to the Blueprint. */
	void HandleDeath();

	/** Cached at BeginPlay, when Blueprint-added components exist. */
	UPROPERTY(Transient)
	TObjectPtr<UDinoAttackComponent> AttackComponent;

	/**
	 * Draws a capsule and label in the current state's colour.
	 *
	 * Debug draw rather than a material tint, on purpose: tinting needs a material with a
	 * colour parameter, which the placeholder meshes do not have, and would silently do
	 * nothing. This works on any mesh with no asset setup at all.
	 *
	 * Runs on every machine from the replicated Awareness, so clients see the same colours
	 * as the host without the AI existing on them.
	 */
	void DrawStateDebug();

	FTimerHandle DebugDrawTimer;
};
