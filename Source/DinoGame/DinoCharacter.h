#pragma once

#include "CoreMinimal.h"
#include "DinoCombatTypes.h"
#include "Engine/Attenuation.h"
#include "GameFramework/Character.h"
#include "DinoCharacter.generated.h"

class ADinoCreature;
class USoundAttenuation;
class UVOIPTalker;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDinoHealthChanged, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDinoDied);

/**
 * Networked base for player characters. Intentionally declares no mesh or camera components:
 * BP_FirstPersonCharacter already owns those, so reparenting onto this class is non-destructive.
 *
 * Aim pitch replication is not implemented here because APawn already replicates it via
 * RemoteViewPitch16 and exposes it through GetBaseAimRotation().
 */
UCLASS()
class DINOGAME_API ADinoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADinoCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	bool IsAlive() const { return CurrentHealth > 0.0f; }

	/** Server only. Clamped to [0, MaxHealth]. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Health")
	void Heal(float Amount);

	/** Fires on every machine, including the server. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Health")
	FDinoHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Dino|Health")
	FDinoDied OnDied;

	/** 0 while silent, rising with speech volume. Driven by the voice engine, not replicated. */
	UFUNCTION(BlueprintPure, Category = "Dino|Voice")
	float GetVoiceLevel() const;

	// --- Restraint ------------------------------------------------------------------------
	// A player held by a creature: pinned under a raptor, or in a T-Rex's jaws. The creature's
	// attack component decides when; this side owns what it does to the player.

	/**
	 * Server only. Stops the player moving and hands the camera hook its cue.
	 *
	 * Ignored for a dead player - a corpse cannot be grabbed - and for Kind None, which is
	 * what EndRestraint is for.
	 */
	void BeginRestraint(ADinoCreature* Captor, EDinoRestraint Kind);

	/** Server only. Safe to call when not restrained. */
	void EndRestraint();

	UFUNCTION(BlueprintPure, Category = "Dino|Restraint")
	bool IsRestrained() const { return Restraint.Kind != EDinoRestraint::None; }

	UFUNCTION(BlueprintPure, Category = "Dino|Restraint")
	EDinoRestraint GetRestraintKind() const { return Restraint.Kind; }

	/** The creature holding this player, or null. What a future jaw camera would attach to. */
	UFUNCTION(BlueprintPure, Category = "Dino|Restraint")
	ADinoCreature* GetRestrainingCreature() const { return Restraint.Captor; }

protected:
	/**
	 * Fires on every machine when this player is grabbed or pinned.
	 *
	 * The camera hook. TODO(devour-camera): override this in BP_FirstPersonCharacter to move the
	 * view into the captor's jaws - the captor's attack component names the socket, via
	 * UDinoAttackComponent::GetGrabSocketName. Check IsLocallyControlled() before touching the
	 * camera: this runs on every machine, and only the victim's own view should change.
	 *
	 * Movement and input are already locked by the time this runs, so an override only needs to
	 * handle presentation. The C++ default does nothing beyond logging.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Dino|Restraint")
	void OnRestraintBegan(ADinoCreature* Captor, EDinoRestraint Kind);

	/** Fires on every machine when the hold ends - freed, or dead. Undo the camera here. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dino|Restraint")
	void OnRestraintEnded();

	UPROPERTY(ReplicatedUsing = OnRep_Restraint, BlueprintReadOnly, Category = "Dino|Restraint")
	FDinoRestraintState Restraint;

	UFUNCTION()
	void OnRep_Restraint(const FDinoRestraintState& OldRestraint);

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	/**
	 * This player's voice source. A UActorComponent, not a scene component — it carries no
	 * transform of its own. Spatialisation comes later from Settings.ComponentToAttachTo plus a
	 * USoundAttenuation asset; while both are null the voice plays unspatialised, which is what
	 * phase 1 wants.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Voice")
	TObjectPtr<UVOIPTalker> VoipTalker;

	/**
	 * Optional hand-tuned attenuation asset. Leave null and a sensible one is built in code from
	 * the values below — assign an asset here only once tuning falloff by ear is worth doing in
	 * the editor rather than in C++.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Voice")
	TObjectPtr<USoundAttenuation> VoiceAttenuation;

	/** Centimetres of full-volume radius before falloff begins. 100 uu = 1 m. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Voice", meta = (ClampMin = "0.0"))
	float VoiceInnerRadius = 500.0f;

	/** Centimetres beyond the inner radius over which the voice falls away to silence. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Voice", meta = (ClampMin = "1.0"))
	float VoiceFalloffDistance = 3000.0f;

	/**
	 * How volume maps to distance. Linear is the predictable one: audible range really is
	 * InnerRadius + FalloffDistance. NaturalSound is more realistic but fades to dBAttenuationAtMax,
	 * which is inaudible well before the stated distance — it reads as half the range you asked for.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Voice")
	EAttenuationDistanceModel VoiceDistanceModel = EAttenuationDistanceModel::Linear;

	/** Cutoff applied when geometry blocks the line to a speaker. Lower is more muffled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Voice", meta = (ClampMin = "20.0"))
	float VoiceOcclusionLowPassHz = 300.0f;

	/** Volume multiplier applied on top of the muffling when occluded. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Voice", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VoiceOcclusionVolume = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, BlueprintReadOnly, Category = "Dino|Health")
	float CurrentHealth = 100.0f;

	UFUNCTION()
	void OnRep_CurrentHealth(float OldHealth);

private:
	void ApplyHealthChange(float OldHealth);

	/**
	 * Everything that has to happen when health reaches zero. Runs on every machine, from the
	 * same place the OnDied broadcast does, so the victim's own client stops predicting
	 * movement at the same moment the server stops accepting it.
	 */
	void HandleDeath();

	/** Applies or undoes restraint effects. Runs on every machine, like ApplyHealthChange. */
	void ApplyRestraintChange(const FDinoRestraintState& OldRestraint);

	/**
	 * The controller whose move input this restraint switched off, so the release switches the
	 * same one back on.
	 *
	 * SetIgnoreMoveInput is a counter, not a flag - every true needs exactly one false. Holding
	 * the controller rather than re-reading GetController() at release keeps that pairing intact
	 * even if possession changed in between; an unpaired true locks movement for good.
	 */
	TWeakObjectPtr<AController> RestraintInputController;

	/**
	 * Binds VoipTalker to this pawn's PlayerState. Called from both PossessedBy and
	 * OnRep_PlayerState because the PlayerState arrives at different times on the server and on
	 * clients, and registering against a null one silently does nothing.
	 */
	void RegisterVoiceTalker();

	/** Returns the assigned asset, or lazily builds one from the tunables above. */
	USoundAttenuation* ResolveVoiceAttenuation();

	/** Holds the code-built attenuation so it is not garbage collected. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeVoiceAttenuation;
};
