#pragma once

#include "CoreMinimal.h"
#include "DinoCombatTypes.h"
#include "Engine/Attenuation.h"
#include "GameFramework/Character.h"
#include "DinoCharacter.generated.h"

class ADinoCreature;
class UDinoFlareGunComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UPrimitiveComponent;
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

	UFUNCTION(BlueprintPure, Category = "Dino|Weapons")
	UDinoFlareGunComponent* GetFlareGun() const { return FlareGun; }

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

	/** Flat direction from this player to their captor when caught. The pinned body's feet point this way. */
	FVector GetPinDirection() const { return Restraint.Direction; }

	/**
	 * Where a pinning creature stands: on the ground, over the lying body's chest. Decided here
	 * because the body's layout is decided here; the raptor moves to it, and the victim's camera
	 * aims at it, so the two cannot disagree.
	 */
	FVector GetPinnedCaptorSpot() const;

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
	 * Binds the flare gun and crouch. Movement, look and jump stay in the Blueprint; these two
	 * are built in code so they need no input assets.
	 *
	 * Crouch: hold Left Ctrl, or press C (right stick click on a pad) to toggle.
	 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Re-applies the pinned pose if a crouch change lands while pinned. ACharacter repositions
	 * the body mesh on every crouch change, and on a watching client the uncrouch can replicate
	 * after the pin - which would stand the body back up mid-pin.
	 */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// --- Pinned pose ---------------------------------------------------------------------------
	// A pinned player lies on their back, feet toward the raptor. Done by laying the body mesh
	// down; the capsule stays upright, so movement and collision are untouched. No animation
	// asset is involved - the body is the standing mesh, turned flat.

	/** Feet to crown of the lying body, used to centre it on the capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Restraint", meta = (ClampMin = "0.0"))
	float PinnedBodyLength = 180.0f;

	/** How far the lying body sits above the ground, so it rests on the floor rather than in it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Restraint")
	float PinnedBodyLift = 10.0f;

	/** Drop the pinned player's own view to where their head now lies, and hide their arms. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Restraint")
	bool bLowerViewWhenPinned = true;

	/** Height of a pinned player's eyes above the ground. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Restraint", meta = (ClampMin = "0.0"))
	float PinnedEyeHeight = 25.0f;

	/**
	 * How far from the middle of the lying body, toward the feet, the raptor stands. 0 is dead
	 * centre; larger moves it down the body, out from directly over the victim's face.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Restraint")
	float PinnedCaptorOffset = 40.0f;

	/** Degrees a pinned player can look left or right of the raptor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Restraint", meta = (ClampMin = "0.0", ClampMax = "179.0"))
	float PinnedLookYawLeeway = 25.0f;

	/** Degrees a pinned player can look up or down from the raptor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Restraint", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float PinnedLookPitchLeeway = 15.0f;

	/**
	 * Every player carries one. Created here in the C++ parent so BP_FirstPersonCharacter
	 * inherits it with no editor work, the same as VoipTalker.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Weapons")
	TObjectPtr<UDinoFlareGunComponent> FlareGun;

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

	/** Lays the body down feet-first toward the captor. Every machine; the view part only locally. */
	void ApplyPinnedPose();

	/** Stands the body back up and restores the view. Safe to call when not posed. */
	void ClearPinnedPose();

	/**
	 * The first-person arms the camera is mounted on, found by its rendering role rather than by
	 * name, so renaming the Blueprint component does not silently break the pinned view.
	 */
	UPrimitiveComponent* FindFirstPersonMesh() const;

	bool bPinnedPoseApplied = false;
	TWeakObjectPtr<UPrimitiveComponent> PosedFirstPersonMesh;
	FTransform SavedFirstPersonMeshTransform;

	// --- Crouch input --------------------------------------------------------------------------

	void HandleCrouchPressed();
	void HandleCrouchReleased();
	void HandleCrouchToggled();

	/** Alive and free. A pinned or dead player cannot crouch. */
	bool CanUseCrouchInput() const;

	/** Removes the crouch mapping. On death, so the keys are free, and on EndPlay. */
	void RemoveCharacterInput();

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrouchHoldAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrouchToggleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> CharacterInputContext;

	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> CharacterInputSubsystem;

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
