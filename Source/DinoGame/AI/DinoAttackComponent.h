#pragma once

#include "CoreMinimal.h"
#include "AI/DinoAITypes.h"
#include "Components/ActorComponent.h"
#include "DinoCombatTypes.h"
#include "DinoAttackComponent.generated.h"

class ADinoAIControllerBase;
class ADinoCharacter;
class ADinoCreature;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDinoAttackPhaseChanged, EDinoAttackPhase, NewPhase, EDinoAttackPhase, OldPhase);

/** Server only. Victim is null for a stagger, or a lunge forced with no target. */
DECLARE_MULTICAST_DELEGATE_OneParam(FDinoAttackBegan, ADinoCharacter* /*Victim*/);

/** Server only. Fires after the phase is already None, so listeners see the creature as free. */
DECLARE_MULTICAST_DELEGATE_OneParam(FDinoAttackFinished, EDinoAttackOutcome /*Outcome*/);

/**
 * How a creature attacks. Added to a species' Blueprint; a creature without one never attacks.
 *
 * A component rather than code on ADinoCreature because attack style is exactly the kind of
 * species difference the creature class is meant to leave to its Blueprint children - a T-Rex
 * devours, a raptor lunges and pins, and a herbivore has no attack at all.
 *
 * Split of responsibility with the AI controller: the controller decides *when* (it owns
 * awareness and targets, and calls TryStartAttack), this decides *how* - timing, movement,
 * who gets hit, and what happens to them. The controller only ever talks to this base class,
 * so a new attack style is a new subclass with no controller changes.
 *
 * All decisions run on the server. Phase and held victim replicate so clients can drive
 * cosmetics and debug draw; nothing else here is meaningful off the server.
 *
 * Stagger lives here too, not on the creature: being knocked off an attack is a phase of the
 * same state machine, and cancelling a lunge or releasing a pinned victim is this component's
 * cleanup to do.
 */
UCLASS(Abstract, ClassGroup = (Dino))
class DINOGAME_API UDinoAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDinoAttackComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Controller-facing -------------------------------------------------------------------

	/**
	 * Starts an attack if one is possible right now. Server only.
	 *
	 * bForced skips the awareness gate and lets a style attack with no victim where that makes
	 * sense. Used by the StateTree task while awareness is debug-locked, so `DinoSetState att`
	 * loops attacks for tuning without anyone needing to stand in the right spot.
	 */
	bool TryStartAttack(bool bForced);

	/**
	 * Knocks the creature off whatever it is doing and releases any victim. Server only.
	 *
	 * Returns false if the current attack refuses interruption - see CanBeInterrupted. Whether
	 * a hit should stagger at all is the creature's call (its vulnerability settings); this
	 * only carries it out.
	 */
	bool BeginStagger(float Duration);

	/** Ends any attack immediately, without a stagger. For death, unpossession, debug. */
	void CancelAttack();

	UFUNCTION(BlueprintPure, Category = "Dino|Attack")
	EDinoAttackPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "Dino|Attack")
	bool IsBusy() const { return Phase != EDinoAttackPhase::None; }

	UFUNCTION(BlueprintPure, Category = "Dino|Attack")
	ADinoCharacter* GetHeldVictim() const { return HeldVictim; }

	/**
	 * Socket on the creature's mesh where a held victim's view or body belongs.
	 *
	 * Half of the jaw-camera hook: nothing reads this yet. When the first-person-in-the-jaws
	 * view is built, ADinoCharacter::OnRestraintBegan attaches to this socket on the captor.
	 * Named here rather than hardcoded so each species' mesh can name its own.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|Attack")
	FName GetGrabSocketName() const { return GrabSocketName; }

	/** Seconds until another attack may start. Server only; 0 elsewhere. */
	float GetCooldownRemaining() const;

	/** Short readout for debug labels and DinoAIStatus. Empty while idle and off cooldown. */
	FString DescribeAttack() const;

	/** Style-specific debug shapes. Called from ADinoCreature::DrawStateDebug on every machine. */
	virtual void DrawDebug(const UWorld* World, float Lifetime) const;

	/** Adds PASS/FAIL lines to DinoAICheck. */
	virtual void AppendSetupCheck(TArray<FString>& OutLines) const;

	/** Formats a line the same way ADinoAIControllerBase::RunSetupCheck does. */
	static FString CheckLine(bool bPass, const FString& Label, const FString& Detail);

	/** Fires on every machine whenever the phase changes. The hook for roars, stings, animation. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Attack")
	FDinoAttackPhaseChanged OnAttackPhaseChanged;

	FDinoAttackBegan OnAttackBegan;
	FDinoAttackFinished OnAttackFinished;

protected:
	// --- Subclass hooks ----------------------------------------------------------------------

	/** Whether this style may start from the creature's current awareness. */
	virtual bool CanTriggerFromAwareness(EDinoAwareness Awareness) const PURE_VIRTUAL(UDinoAttackComponent::CanTriggerFromAwareness, return false;);

	/** Who to attack right now, or null if nobody qualifies. */
	virtual ADinoCharacter* SelectVictim(bool bForced) const PURE_VIRTUAL(UDinoAttackComponent::SelectVictim, return nullptr;);

	/** Whether an attack may start with no victim at all. */
	virtual bool CanAttackWithoutVictim(bool bForced) const { return false; }

	/** Starts the style's first phase. The creature has already been told to stop moving. */
	virtual void BeginAttack(ADinoCharacter* Victim) PURE_VIRTUAL(UDinoAttackComponent::BeginAttack, );

	/** Called when the current phase's duration runs out. The base handles Staggered. */
	virtual void OnPhaseTimerElapsed();

	/** Per-frame work for phases started with bTick. Server only. */
	virtual void TickPhase(float DeltaTime) {}

	/**
	 * Whether a stagger may interrupt what this component is doing right now.
	 *
	 * The T-Rex devour overrides this to refuse. That is deliberately enforced here, not only by
	 * the T-Rex having stagger switched off in its vulnerability settings - so a Blueprint
	 * misconfigured to be staggerable still cannot be made to drop a player mid-devour.
	 */
	virtual bool CanBeInterrupted() const { return true; }

	/** Style-specific teardown - stop root motion, clear timers. Runs before any finish or stagger. */
	virtual void CleanUpAttack() {}

	// --- Helpers for subclasses ----------------------------------------------------------------

	/**
	 * Changes phase, replicates it, and schedules OnPhaseTimerElapsed after Duration (0 = none).
	 * bTick enables TickPhase for the phase; ticking is off otherwise, so an idle creature costs
	 * nothing.
	 */
	void SetPhase(EDinoAttackPhase NewPhase, float Duration = 0.0f, bool bTick = false);

	/** Releases the victim, starts the cooldown, returns to None, and tells the controller. */
	void FinishAttack(EDinoAttackOutcome Outcome);

	void HoldVictim(ADinoCharacter* Victim, EDinoRestraint Kind);
	void ReleaseVictim();

	/** Alive, not held by anything, and visible from the creature's eyes. */
	bool IsValidVictim(const ADinoCharacter* Victim) const;

	/**
	 * Clear Visibility line from the creature's eyes to the actor.
	 *
	 * From the eyes rather than the mouth because it is the same point perception uses, so an
	 * attack can never land on someone the creature could not have seen - no bites through walls.
	 */
	bool HasClearLineTo(const AActor* Other) const;

	/** Server only. Goes through ApplyPointDamage, so the victim's own TakeDamage decides the rest. */
	void DealDamage(ADinoCharacter* Victim, float Amount, const FVector& FromDirection) const;

	/**
	 * Horizontal gap between the two capsules' surfaces. Measured edge to edge so ranges mean
	 * the same thing whatever size the creature is - a T-Rex and a raptor with the same
	 * trigger range reach the same distance past their own bodies.
	 */
	float EdgeDistance2D(const AActor* Other) const;

	ADinoCreature* GetCreature() const;
	ADinoAIControllerBase* GetAIController() const;
	float GetCapsuleRadius() const;
	double GetTimeSeconds() const;

	/** Seconds after an attack ends before another can begin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack", meta = (ClampMin = "0.0"))
	float AttackCooldown = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Attack")
	FName GrabSocketName = TEXT("Jaw");

	UPROPERTY(ReplicatedUsing = OnRep_Phase, BlueprintReadOnly, Category = "Dino|Attack")
	EDinoAttackPhase Phase = EDinoAttackPhase::None;

	UFUNCTION()
	void OnRep_Phase(EDinoAttackPhase OldPhase);

	/** Replicated so debug draw can link captor and victim on clients too. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dino|Attack")
	TObjectPtr<ADinoCharacter> HeldVictim;

	/** Who the attack was aimed at when it started. May be null. Server only. */
	TWeakObjectPtr<ADinoCharacter> AttackTarget;

	double PhaseStartedAt = 0.0;

private:
	FTimerHandle PhaseTimer;
	double CooldownEndsAt = 0.0;
};
