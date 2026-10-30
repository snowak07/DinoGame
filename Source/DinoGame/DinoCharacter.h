#pragma once

#include "CoreMinimal.h"
#include "DinoCombatTypes.h"
#include "Engine/Attenuation.h"
#include "GameFramework/Character.h"
#include "Perception/AISightTargetInterface.h"
#include "DinoCharacter.generated.h"

class ADinoCreature;
class UDamageType;
class UDinoFlareGunComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UPrimitiveComponent;
class USoundAttenuation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDinoHealthChanged, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDinoDied);

class ADinoCharacter;

/**
 * Health, downed, and the bleed-out meter, replicated as one property on purpose - the same
 * reasoning as FDinoPlayerStatusState. As separate properties a client could see health reach
 * zero a frame before "downed" arrived, and play a death that never happened.
 * The rules that change it are DinoHealthRules.
 */
USTRUCT(BlueprintType)
struct FDinoVitalsState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Health")
	float Health = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Health")
	bool bDowned = false;

	/** 0 - 100 while downed. At 0 they are dead. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|Health")
	float BleedOut = 0.0f;
};

/** A revive in progress on this (downed) player: who, and how far along. For both players' screens. */
USTRUCT(BlueprintType)
struct FDinoReviveState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Health")
	float Progress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Health")
	TObjectPtr<ADinoCharacter> Reviver = nullptr;
};

/**
 * Networked base for player characters. Intentionally declares no mesh or camera components:
 * BP_FirstPersonCharacter already owns those, so reparenting onto this class is non-destructive.
 *
 * Aim pitch replication is not implemented here because APawn already replicates it via
 * RemoteViewPitch16 and exposes it through GetBaseAimRotation().
 */
UCLASS()
class DINOGAME_API ADinoCharacter : public ACharacter, public IAISightTargetInterface
{
	GENERATED_BODY()

public:
	ADinoCharacter();

	/**
	 * How creatures' sight tests this player. Crouched, they are seen only within
	 * dino.Stealth.CrouchSightFactor of a creature's sight range; otherwise - and within that -
	 * exactly the engine's own test: one line from the creature's eyes to this capsule's centre,
	 * on the AI system's sight channel, clear or blocked only by this player's own parts.
	 *
	 * Implementing the hook means the engine asks this and nothing else for players, so the line
	 * test cannot be skipped: answering "undefined" is not handed back to the default.
	 */
	virtual UAISense_Sight::EVisibilityResult CanBeSeenFrom(const FCanBeSeenFromContext& Context, FVector& OutSeenLocation,
		int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested, float& OutSightStrength,
		int32* UserData = nullptr, const FOnPendingVisibilityQueryProcessedDelegate* Delegate = nullptr) override;

	virtual void Tick(float DeltaSeconds) override;

	/** Server: landing from a jump or a fall is a noise (dino.Noise.LandingRange). */
	virtual void Landed(const FHitResult& Hit) override;

	/** Ticks only for the locally controlled player, whose crouch view is the only per-frame work here. */
	virtual void NotifyControllerChanged() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/**
	 * Below the level's Kill Z. Unreal's default deletes the character outright, which skipped
	 * every part of a death - no spectating, and the player stayed counted as alive, so the round
	 * could never end. Routed through ADinoGameMode::KillPlayer instead, as a Fall.
	 */
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;

	/**
	 * Server only. Kills the body - health to zero, with everything that follows on every
	 * machine - for deaths that are not lethal damage. Called by ADinoGameMode::KillPlayer; call
	 * that rather than this, so the player's status changes too. Safe when already dead.
	 */
	void Die(TSubclassOf<UDamageType> Cause);

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	float GetCurrentHealth() const { return Vitals.Health; }

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	float GetMaxHealth() const { return MaxHealth; }

	/** Not dead: standing, or down and still able to be revived. */
	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	bool IsAlive() const { return Vitals.Health > 0.0f || Vitals.bDowned; }

	/** Down: cannot move, bleeding out, waiting for a teammate. Still alive. */
	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	bool IsDowned() const { return Vitals.bDowned; }

	/** Alive and on their feet. */
	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	bool IsStanding() const { return Vitals.Health > 0.0f && !Vitals.bDowned; }

	/** 0 - 100 while downed. */
	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	float GetBleedOut() const { return Vitals.BleedOut; }

	const FDinoReviveState& GetReviveState() const { return ReviveState; }

	/** Server only. Clamped to [0, MaxHealth]. Only while standing. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Health")
	void Heal(float Amount);

	/** Server only. Stands a downed player back up with HealthFraction of max health. Called by ADinoGameMode::RevivePlayer. */
	void Revive(float HealthFraction);

	/**
	 * The downed teammate this player would revive right now: down, not held by a creature,
	 * within dino.Downed.ReviveRange and roughly in front of them. Null when there is none. Any
	 * machine - for the revive key and the on-screen prompt.
	 */
	ADinoCharacter* FindReviveTarget() const;

	/** Revive key held: start reviving Target. The server checks everything again, every tick. */
	UFUNCTION(Server, Reliable)
	void ServerBeginRevive(ADinoCharacter* Target);

	/** Revive key released. */
	UFUNCTION(Server, Reliable)
	void ServerStopRevive();

	/** Fires on every machine, including the server. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Health")
	FDinoHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Dino|Health")
	FDinoDied OnDied;

	/** 0 while silent, rising with speech volume. Driven by the voice engine, not replicated. */
	/** This player's current voice level, 0 when silent. From their PlayerState's voice talker. */
	UFUNCTION(BlueprintPure, Category = "Dino|Voice")
	float GetVoiceLevel() const;

	/**
	 * The proximity and occlusion settings this body's voice plays with: the assigned
	 * VoiceAttenuation asset, or one built from the tunables below. Used by the player's voice
	 * talker while they are alive (ADinoPlayerState::RefreshVoicePlacement).
	 */
	USoundAttenuation* GetVoiceAttenuation();

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

	/** Lying on the ground: pinned, or down. For the self-check and tests. */
	bool IsLyingDown() const { return bPinnedPoseApplied; }

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
	 * inherits it with no editor work.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Weapons")
	TObjectPtr<UDinoFlareGunComponent> FlareGun;

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

	/** Eye height above the floor while crouched, cm. The camera eases down to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Crouch", meta = (ClampMin = "20.0"))
	float CrouchedViewHeight = 100.0f;

	/** How quickly the view eases down and back up. Higher is snappier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Crouch", meta = (ClampMin = "0.5"))
	float CrouchViewSpeed = 10.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Vitals, BlueprintReadOnly, Category = "Dino|Health")
	FDinoVitalsState Vitals;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dino|Health")
	FDinoReviveState ReviveState;

	UFUNCTION()
	void OnRep_Vitals(const FDinoVitalsState& OldVitals);

private:
	/**
	 * Everything that follows a change in health or state, on every machine: from each change on
	 * the server, and from the RepNotify elsewhere. Going down, being revived and dying each run
	 * from here, decided by comparing before and after, so the same transitions happen everywhere.
	 */
	void ApplyVitalsChange(const FDinoVitalsState& OldVitals);

	/** Every machine. Stops movement, lays the body down as a pin does, starts bleeding out on the server. */
	void HandleDowned();

	/** Every machine. Back on their feet, out of the lying pose. */
	void HandleRevived();

	/** Server, while downed: the bleed-out meter drains (DinoHealthRules::Bleed). */
	void BleedTick();
	FTimerHandle BleedTimer;

	/** The last quarter of bleed-out logged, so the log marks 75 / 50 / 25 % left, not every tick. */
	int32 LastBleedQuarterLogged = 4;
	void LogBleedOut();

	/** Server: the damage type that downed this player, for the log and the game mode. */
	TSubclassOf<UDamageType> PendingDownCause;

	// --- Reviving someone else ------------------------------------------------------------------

	/** Server, while this player revives someone: checks it can go on, and advances it. */
	void ReviveTick();
	void StopReviving(const TCHAR* Reason);
	FTimerHandle ReviveTimer;
	TWeakObjectPtr<ADinoCharacter> ReviveTarget;

	void HandleRevivePressed();
	void HandleReviveReleased();

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ReviveAction;

	/** The lowered view: eases toward ViewHeight above the floor. Crouching uses it; down uses the pinned pose. */
	void BeginLoweredView(float ViewHeight);

	/**
	 * Server only. The damage type of the hit that is killing this character, recorded before
	 * health is applied so the death can report it. A Bite for a pin, a Devour for a T-Rex.
	 */
	TSubclassOf<UDamageType> PendingDeathCause;

	/**
	 * Everything that has to happen when health reaches zero. Runs on every machine, from the
	 * same place the OnDied broadcast does, so the victim's own client stops predicting
	 * movement at the same moment the server stops accepting it.
	 */
	void HandleDeath();

	/** Applies or undoes restraint effects. Runs on every machine, like ApplyHealthChange. */
	void ApplyRestraintChange(const FDinoRestraintState& OldRestraint);

	/**
	 * Lays the body down feet-first toward the captor. Every machine; the view part only locally.
	 * Pinned and downed players both lie in it; it stays while either holds.
	 */
	void ApplyPinnedPose();

	/** Stands the body back up and restores the view. Safe to call when not posed. */
	void ClearPinnedPose();

	/**
	 * The first-person arms the camera is mounted on, found by its rendering role rather than by
	 * name, so renaming the Blueprint component does not silently break the pinned view.
	 */
	UPrimitiveComponent* FindFirstPersonMesh() const;

	bool bPinnedPoseApplied = false;

	/** The way the lying body's feet point, kept so a pin ending while down leaves the body where it lies. */
	FVector PoseDirection = FVector::ZeroVector;

	// --- Crouch view ---------------------------------------------------------------------------
	// The camera rides the first-person arms, and on a crouch the engine keeps those where they
	// were - it moves the body up as the capsule shrinks, so the feet stay on the ground. Without
	// a crouch animation to move the head, the view never dropped. These ease the arms down
	// instead, locally; the pinned pose owns the arms while it is applied.

	/** On crouching: how far down the view should go, measured from where the camera is now. */
	void BeginCrouchView();

	/** Every frame, locally: eases the arms toward the wanted drop. */
	void UpdateCrouchView(float DeltaSeconds);

	float CrouchViewDrop = 0.0f;
	float CrouchViewDropTarget = 0.0f;
	FVector ArmsStandingLocation = FVector::ZeroVector;
	bool bHaveArmsStandingLocation = false;

	// --- Footsteps -----------------------------------------------------------------------------

	/** Server, a few times a second: walking makes noise (DinoNoiseRules::FootstepRange). */
	void ReportFootsteps();
	FTimerHandle FootstepTimer;

	/** The last footstep range reported, so the log says when it changes, not every step. */
	float LastFootstepRange = -1.0f;
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
	 * Tells this player's PlayerState that this is their body, so their voice comes from it.
	 * Called from BeginPlay, PossessedBy and OnRep_PlayerState, because the PlayerState arrives at
	 * different times on the server and on clients. The voice talker itself lives on the
	 * PlayerState (UDinoVoiceTalker explains why).
	 */
	void BindVoiceToPlayerState();

	/** Holds the code-built attenuation so it is not garbage collected. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeVoiceAttenuation;
};
