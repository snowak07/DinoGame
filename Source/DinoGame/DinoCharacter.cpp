#include "DinoCharacter.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoCreature.h"
#include "AI/DinoNoiseRules.h"
#include "AI/DinoNoiseSubsystem.h"
#include "AI/DinoStealth.h"
#include "AI/DinoTargetRules.h"
#include "AISystem.h"
#include "TimerManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DinoDamageTypes.h"
#include "Engine/DamageEvents.h"
#include "DinoGame.h"
#include "DinoGameMode.h"
#include "DinoPlayerController.h"
#include "DinoPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundAttenuation.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Weapons/DinoFlareGunComponent.h"

ADinoCharacter::ADinoCharacter()
{
	// Ticks only while locally controlled, for the crouch view - see NotifyControllerChanged.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// The inherited mesh is the third-person body other players see. The owner sees only the
	// first-person arms, which live on the Blueprint.
	GetMesh()->SetOwnerNoSee(true);

	// Adding this to the C++ parent propagates it to BP_FirstPersonCharacter as an inherited
	// component, so the reparented Blueprint picks it up without any editor work.

	FlareGun = CreateDefaultSubobject<UDinoFlareGunComponent>(TEXT("FlareGun"));

	// Off by default in the engine; without it Crouch() silently does nothing.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	}
}

void ADinoCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	APlayerController* PC = Cast<APlayerController>(GetController());

	if (FlareGun)
	{
		FlareGun->BindInput(Input, PC);
	}

	if (!Input || !PC)
	{
		return;
	}

	// Rebuilt on every possession, so drop any context left from the last one first.
	RemoveCharacterInput();

	CrouchHoldAction = NewObject<UInputAction>(this, TEXT("CrouchHold"));
	CrouchHoldAction->ValueType = EInputActionValueType::Boolean;
	CrouchToggleAction = NewObject<UInputAction>(this, TEXT("CrouchToggle"));
	CrouchToggleAction->ValueType = EInputActionValueType::Boolean;

	CharacterInputContext = NewObject<UInputMappingContext>(this, TEXT("CharacterContext"));
	CharacterInputContext->MapKey(CrouchHoldAction, EKeys::LeftControl);
	CharacterInputContext->MapKey(CrouchToggleAction, EKeys::C);
	CharacterInputContext->MapKey(CrouchToggleAction, EKeys::Gamepad_RightThumbstick);

	if (const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			// Same priority as the flare gun: above the template's contexts.
			Subsystem->AddMappingContext(CharacterInputContext, 5);
			CharacterInputSubsystem = Subsystem;
		}
	}

	Input->BindAction(CrouchHoldAction, ETriggerEvent::Started, this, &ADinoCharacter::HandleCrouchPressed);
	Input->BindAction(CrouchHoldAction, ETriggerEvent::Completed, this, &ADinoCharacter::HandleCrouchReleased);
	Input->BindAction(CrouchToggleAction, ETriggerEvent::Started, this, &ADinoCharacter::HandleCrouchToggled);
}

void ADinoCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveCharacterInput();

	if (ADinoPlayerState* DinoState = GetPlayerState<ADinoPlayerState>())
	{
		DinoState->ClearVoiceBody(this);
	}

	Super::EndPlay(EndPlayReason);
}

// --- Crouch ----------------------------------------------------------------------------------

bool ADinoCharacter::CanUseCrouchInput() const
{
	return IsAlive() && !IsRestrained();
}

void ADinoCharacter::HandleCrouchPressed()
{
	if (CanUseCrouchInput())
	{
		Crouch();
	}
}

void ADinoCharacter::HandleCrouchReleased()
{
	// Unconditional: letting go should always stand you up, whatever happened mid-hold.
	UnCrouch();
}

void ADinoCharacter::HandleCrouchToggled()
{
	if (!CanUseCrouchInput())
	{
		return;
	}

	// bWantsToCrouch rather than bIsCrouched: it is the request, which is what a toggle flips.
	// bIsCrouched lags a frame behind it, and a quick double press would read the stale value.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement && Movement->bWantsToCrouch)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void ADinoCharacter::RemoveCharacterInput()
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = CharacterInputSubsystem.Get())
	{
		Subsystem->RemoveMappingContext(CharacterInputContext);
	}
	CharacterInputSubsystem.Reset();
}

void ADinoCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (bPinnedPoseApplied)
	{
		ApplyPinnedPose();
	}

	BeginCrouchView();
}

void ADinoCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (bPinnedPoseApplied)
	{
		ApplyPinnedPose();
	}

	CrouchViewDropTarget = 0.0f;
}

void ADinoCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	SetActorTickEnabled(IsLocallyControlled());
}

void ADinoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateCrouchView(DeltaSeconds);
}

void ADinoCharacter::BeginCrouchView()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	const UCameraComponent* Camera = FindComponentByClass<UCameraComponent>();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Camera || !Capsule)
	{
		return;
	}

	// Measured rather than assumed: where the camera would be with no drop applied, above the
	// feet, now that the capsule has shrunk - whatever the arms are attached to, and whatever the
	// engine already moved. The drop is whatever is left to reach CrouchedViewHeight.
	const float FeetZ = Capsule->GetComponentLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	const float RawHeight = Camera->GetComponentLocation().Z + CrouchViewDrop - FeetZ;
	CrouchViewDropTarget = FMath::Max(0.0f, RawHeight - CrouchedViewHeight);

	UE_LOG(LogDinoGame, Log, TEXT("Crouch: view lowering %.0f cm (camera %.0f cm above the floor, crouched view %.0f cm)."),
		CrouchViewDropTarget, RawHeight, CrouchedViewHeight);
}

void ADinoCharacter::UpdateCrouchView(float DeltaSeconds)
{
	USceneComponent* Arms = FindFirstPersonMesh();
	if (!Arms || !IsLocallyControlled())
	{
		return;
	}

	// The pinned pose places the arms itself, and restores them when it ends.
	if (bPinnedPoseApplied)
	{
		return;
	}

	if (!bHaveArmsStandingLocation)
	{
		ArmsStandingLocation = Arms->GetRelativeLocation();
		bHaveArmsStandingLocation = true;
	}

	if (FMath::IsNearlyEqual(CrouchViewDrop, CrouchViewDropTarget, 0.1f) && FMath::IsNearlyZero(CrouchViewDropTarget))
	{
		if (!FMath::IsNearlyZero(CrouchViewDrop))
		{
			CrouchViewDrop = 0.0f;
			Arms->SetRelativeLocation(ArmsStandingLocation);
		}
		return;
	}

	CrouchViewDrop = FMath::FInterpTo(CrouchViewDrop, CrouchViewDropTarget, DeltaSeconds, CrouchViewSpeed);

	// Straight down in the world, expressed in whatever the arms are attached to.
	const USceneComponent* Parent = Arms->GetAttachParent();
	const FVector Down = Parent
		? Parent->GetComponentTransform().InverseTransformVectorNoScale(FVector(0.0f, 0.0f, -CrouchViewDrop))
		: FVector(0.0f, 0.0f, -CrouchViewDrop);
	Arms->SetRelativeLocation(ArmsStandingLocation + Down);
}

UAISense_Sight::EVisibilityResult ADinoCharacter::CanBeSeenFrom(const FCanBeSeenFromContext& Context, FVector& OutSeenLocation,
	int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested, float& OutSightStrength,
	int32* /*UserData*/, const FOnPendingVisibilityQueryProcessedDelegate* /*Delegate*/)
{
	OutNumberOfLoSChecksPerformed = 0;
	OutNumberOfAsyncLosCheckRequested = 0;
	OutSightStrength = 1.0f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return UAISense_Sight::EVisibilityResult::NotVisible;
	}

	const FVector TargetLocation = GetActorLocation();

	// Crouched: only the near part of its range. The engine has already checked the full range
	// and the cone before asking.
	if (bIsCrouched)
	{
		const APawn* Observer = Cast<APawn>(Context.IgnoreActor);
		if (const ADinoAIControllerBase* Brain = Observer ? Cast<ADinoAIControllerBase>(Observer->GetController()) : nullptr)
		{
			const bool bAlreadySeen = Context.bWasVisible && *Context.bWasVisible;
			const float Range = DinoTargetRules::StealthRange(Brain->GetSightRange(bAlreadySeen), true, DinoStealth::CrouchSightFactor());
			if (FVector::DistSquared(Context.ObserverLocation, TargetLocation) > FMath::Square(Range))
			{
				return UAISense_Sight::EVisibilityResult::NotVisible;
			}
		}
	}

	// The engine's own test, as UAISense_Sight::ComputeVisibility runs it for any target without
	// this hook: the configured sight channel, complex collision, ignoring the observer.
	static const ECollisionChannel SightChannel = []()
	{
		const FByteProperty* Property = FindFProperty<FByteProperty>(UAISystem::StaticClass(), TEXT("DefaultSightCollisionChannel"));
		return Property
			? static_cast<ECollisionChannel>(Property->GetPropertyValue_InContainer(GetDefault<UAISystem>()))
			: ECC_Visibility;
	}();

	const FCollisionQueryParams Params(SCENE_QUERY_STAT(AILineOfSight), true, Context.IgnoreActor);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Context.ObserverLocation, TargetLocation, SightChannel, Params);
	++OutNumberOfLoSChecksPerformed;

	const AActor* HitActor = bHit ? Hit.HitObjectHandle.FetchActor() : nullptr;
	if (!bHit || (HitActor && HitActor->IsOwnedBy(this)))
	{
		OutSeenLocation = TargetLocation;
		return UAISense_Sight::EVisibilityResult::Visible;
	}
	return UAISense_Sight::EVisibilityResult::NotVisible;
}

void ADinoCharacter::ReportFootsteps()
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	UDinoNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UDinoNoiseSubsystem>() : nullptr;
	if (!Movement || !Noise || !IsAlive() || IsRestrained())
	{
		return;
	}

	const float Speed = GetVelocity().Size2D();
	const float Range = DinoNoiseRules::FootstepRange(Speed, Movement->GetMaxSpeed(), bIsCrouched,
		Movement->IsMovingOnGround(), DinoStealth::FootstepRange());

	// Logged as it changes - starting to walk, crouching, stopping - not every step.
	if (!FMath::IsNearlyEqual(Range, LastFootstepRange, 50.0f) || (Range == 0.0f) != (LastFootstepRange == 0.0f))
	{
		UE_LOG(LogDinoGame, Log, TEXT("Footsteps: %s %s (%.0f cm/s%s)."), *GetName(),
			Range > 0.0f ? *FString::Printf(TEXT("carry %.1f m"), Range / 100.0f) : TEXT("silent"),
			Speed, bIsCrouched ? TEXT(", crouched") : TEXT(""));
		LastFootstepRange = Range;
	}

	if (Range > 0.0f)
	{
		Noise->ReportNoise(GetActorLocation(), Range, this, TEXT("footsteps"),
			FString::Printf(TEXT("footsteps (%.0f cm/s)"), Speed), TEXT("footsteps"));
	}
}

void ADinoCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	UDinoNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UDinoNoiseSubsystem>() : nullptr;
	if (!HasAuthority() || !Noise || !IsAlive())
	{
		return;
	}

	UE_LOG(LogDinoGame, Log, TEXT("Footsteps: %s landed (carries %.1f m)."), *GetName(), DinoStealth::LandingRange() / 100.0f);
	Noise->ReportNoise(GetActorLocation(), DinoStealth::LandingRange(), this, TEXT("landing"), TEXT("landing"), TEXT("a landing"));
}

void ADinoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADinoCharacter, CurrentHealth);
	DOREPLIFETIME(ADinoCharacter, Restraint);
}

void ADinoCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		CurrentHealth = MaxHealth;

		// Every player's footsteps, judged where movement is authoritative, so the host and
		// clients make noise by the same rule, in Play In Editor and packaged alike.
		GetWorldTimerManager().SetTimer(FootstepTimer, this, &ADinoCharacter::ReportFootsteps, 0.4f, true);
	}

	SetActorTickEnabled(IsLocallyControlled());

	// Covers the listen-server host, whose PlayerState already exists by BeginPlay.
	BindVoiceToPlayerState();
}

void ADinoCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server path: the PlayerState is assigned during possession.
	BindVoiceToPlayerState();
}

void ADinoCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client path: the PlayerState replicates in separately from the pawn, often after BeginPlay.
	BindVoiceToPlayerState();
}

void ADinoCharacter::BindVoiceToPlayerState()
{
	if (ADinoPlayerState* DinoState = GetPlayerState<ADinoPlayerState>())
	{
		DinoState->SetVoiceBody(this);
	}
}

USoundAttenuation* ADinoCharacter::GetVoiceAttenuation()
{
	if (VoiceAttenuation)
	{
		return VoiceAttenuation;
	}

	if (RuntimeVoiceAttenuation)
	{
		return RuntimeVoiceAttenuation;
	}

	RuntimeVoiceAttenuation = NewObject<USoundAttenuation>(this, TEXT("RuntimeVoiceAttenuation"));

	FSoundAttenuationSettings& Settings = RuntimeVoiceAttenuation->Attenuation;

	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(VoiceInnerRadius, 0.0f, 0.0f);
	Settings.FalloffDistance = VoiceFalloffDistance;
	Settings.DistanceAlgorithm = VoiceDistanceModel;
	// Only consulted by NaturalSound. Kept well above the -60 dB first tried here: that is far
	// below the audible floor of a game mix, so the voice vanished at roughly half the distance
	// the settings claimed. Linear ignores this entirely.
	Settings.dBAttenuationAtMax = -36.0f;

	// Occlusion traces from listener to speaker and muffles through anything blocking it. The
	// trace runs per voice source on an interval, so it is cheap at co-op player counts.
	Settings.bEnableOcclusion = true;
	Settings.OcclusionTraceChannel = ECC_Visibility;
	Settings.OcclusionLowPassFilterFrequency = VoiceOcclusionLowPassHz;
	Settings.OcclusionVolumeAttenuation = VoiceOcclusionVolume;
	// Ramp rather than snap, so walking through a doorway does not click.
	Settings.OcclusionInterpolationTime = 0.15f;
	// Simple collision is enough for walls and terrain and far cheaper than per-triangle.
	Settings.bUseComplexCollisionForOcclusion = false;

	return RuntimeVoiceAttenuation;
}

float ADinoCharacter::GetVoiceLevel() const
{
	const ADinoPlayerState* DinoState = GetPlayerState<ADinoPlayerState>();
	return DinoState ? DinoState->GetVoiceLevel() : 0.0f;
}

float ADinoCharacter::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || ActualDamage <= 0.0f || !IsAlive())
	{
		return 0.0f;
	}

	PendingDeathCause = DamageEvent.DamageTypeClass;

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - ActualDamage);
	ApplyHealthChange(OldHealth);

	return ActualDamage;
}

void ADinoCharacter::Die(TSubclassOf<UDamageType> Cause)
{
	if (!HasAuthority() || !IsAlive())
	{
		return;
	}

	PendingDeathCause = Cause;

	const float OldHealth = CurrentHealth;
	CurrentHealth = 0.0f;
	ApplyHealthChange(OldHealth);
}

void ADinoCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	// Not Super: that deletes the character. Clients do nothing - the server decides deaths, and
	// the engine itself only acts on the authority here.
	if (!HasAuthority() || !IsAlive())
	{
		return;
	}

	// Always a Fall, whatever the level's KillZDamageType says, so no World Settings change is
	// needed for the cause to read right.
	if (ADinoGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ADinoGameMode>() : nullptr)
	{
		GameMode->KillPlayer(GetController(), UDinoDamageType_Fall::StaticClass());
	}
	else
	{
		Die(UDinoDamageType_Fall::StaticClass());
	}
}

void ADinoCharacter::Heal(float Amount)
{
	if (!HasAuthority() || Amount <= 0.0f || !IsAlive())
	{
		return;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + Amount);
	ApplyHealthChange(OldHealth);
}

void ADinoCharacter::OnRep_CurrentHealth(float OldHealth)
{
	ApplyHealthChange(OldHealth);
}

void ADinoCharacter::ApplyHealthChange(float OldHealth)
{
	// Reached from OnRep on simulated proxies and called directly on the authority, since a
	// RepNotify never fires on the machine that wrote the value.
	OnHealthChanged.Broadcast(CurrentHealth, CurrentHealth - OldHealth);

	// There is no health HUD yet, so without this a player has no way to know they were hit
	// short of reading the log. Local only: DinoScreenLog prints on whichever machine calls it,
	// and on a listen server the host would otherwise see every client's damage.
	if (IsLocallyControlled() && CurrentHealth < OldHealth && CurrentHealth > 0.0f)
	{
		DinoScreenLog(FString::Printf(TEXT("Hit for %.0f  (%.0f / %.0f)"),
			OldHealth - CurrentHealth, CurrentHealth, MaxHealth), FColor(240, 150, 40), 3.0f);
	}

	if (CurrentHealth <= 0.0f && OldHealth > 0.0f)
	{
		UE_LOG(LogDinoGame, Log, TEXT("%s died."), *GetName());
		HandleDeath();
		OnDied.Broadcast();
	}
}

void ADinoCharacter::HandleDeath()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// The body leaves the world the moment its owner dies: hidden on every machine, with nothing
	// left to collide with. The actor itself survives until its owner starts spectating
	// (ADinoGameMode::BeginSpectating destroys it then), because the dying player is still
	// looking through its camera for the few seconds of "You died".
	//
	// Stopping creatures *targeting* the dead is a separate job - perception tracks stimulus
	// sources, not visibility or collision - and lives in ADinoAIControllerBase::IsValidTarget.
	//
	// The same for every death, whatever caused it - eaten, pinned, or anything added later.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	// The voice stops coming from this body now. The player status says the same once it
	// arrives; whichever reaches this machine first, the answer is the same.
	if (ADinoPlayerState* VoiceState = GetPlayerState<ADinoPlayerState>())
	{
		VoiceState->RefreshVoicePlacement();
	}

	if (IsLocallyControlled())
	{
		// The pawn's input only - the host/join menu is bound on the PlayerController, so a dead
		// player can still open it and leave rather than being stuck until someone ends the game.
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			DisableInput(PC);
		}

		// Frees LMB before the spectator camera claims it. DisableInput already stops the shot,
		// but a mapping left behind would still swallow the key.
		if (FlareGun)
		{
			FlareGun->UnbindInput();
		}
		RemoveCharacterInput();

		// TODO(death-screen): placeholder until there is a real death screen and respawn.
		DinoScreenLog(TEXT("You died."), FColor(220, 50, 50), 30.0f);
	}

	if (HasAuthority())
	{
		// A dead player is no longer held. Released here rather than left to the captor, so the
		// restraint cannot outlive the victim whatever killed them.
		EndRestraint();

		// The player's side of the death - status, spectating, the round-end check - is the game
		// mode's. A no-op when the game mode started this death itself (KillPlayer -> Die).
		if (ADinoGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ADinoGameMode>() : nullptr)
		{
			GameMode->KillPlayer(GetController(), PendingDeathCause);
		}
	}
}

// --- Restraint ------------------------------------------------------------------------------

void ADinoCharacter::BeginRestraint(ADinoCreature* Captor, EDinoRestraint Kind)
{
	if (!HasAuthority() || !IsAlive() || Kind == EDinoRestraint::None)
	{
		return;
	}

	const FDinoRestraintState OldRestraint = Restraint;
	Restraint.Kind = Kind;
	Restraint.Captor = Captor;

	FVector ToCaptor = Captor ? (Captor->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
	if (ToCaptor.IsNearlyZero())
	{
		ToCaptor = GetActorForwardVector().GetSafeNormal2D();
	}
	Restraint.Direction = ToCaptor;

	// RepNotify never fires on the machine that wrote the value.
	ApplyRestraintChange(OldRestraint);
}

void ADinoCharacter::EndRestraint()
{
	if (!HasAuthority() || Restraint.Kind == EDinoRestraint::None)
	{
		return;
	}

	const FDinoRestraintState OldRestraint = Restraint;
	Restraint = FDinoRestraintState();
	ApplyRestraintChange(OldRestraint);
}

void ADinoCharacter::OnRep_Restraint(const FDinoRestraintState& OldRestraint)
{
	ApplyRestraintChange(OldRestraint);
}

void ADinoCharacter::ApplyRestraintChange(const FDinoRestraintState& OldRestraint)
{
	const bool bWasRestrained = OldRestraint.Kind != EDinoRestraint::None;
	const bool bIsRestrained = Restraint.Kind != EDinoRestraint::None;
	UCharacterMovementComponent* Movement = GetCharacterMovement();

	if (bIsRestrained && !bWasRestrained)
	{
		// Stand up first: a crouched capsule would put the restraint pose, and the body restored
		// on release, at the wrong height. Done where the crouch is actually decided - the server
		// and the victim's own client; watching clients get it through replication, and
		// OnEndCrouch re-applies the pose if that arrives after the pin.
		if (Movement && bIsCrouched && (HasAuthority() || IsLocallyControlled()))
		{
			Movement->bWantsToCrouch = false;
			Movement->UnCrouch(false);
		}

		// On every machine, including the victim's own client. If only the server stopped the
		// movement, the client would keep predicting moves the server then rejects, and a pinned
		// player would jitter against the correction. DisableMovement also blocks jumping, which
		// ignoring move input alone would not.
		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}

		// Devoured victims stay upright, in the jaws; the jaw camera will take over there.
		if (Restraint.Kind == EDinoRestraint::Pinned)
		{
			ApplyPinnedPose();
		}

		if (IsLocallyControlled())
		{
			// Move input only - look input stays live, so the victim can look around and see
			// what has them. That is most of the horror, and the point of not cutting straight
			// to a death screen.
			if (AController* InputController = GetController())
			{
				InputController->SetIgnoreMoveInput(true);
				RestraintInputController = InputController;
			}

			DinoScreenLog(Restraint.Kind == EDinoRestraint::Devoured
				? TEXT("It has you.")
				: TEXT("You're pinned! Someone has to hit it off you."),
				FColor(220, 50, 50), 6.0f);
		}

		OnRestraintBegan(Restraint.Captor, Restraint.Kind);
	}
	else if (!bIsRestrained && bWasRestrained)
	{
		// Only the living stand back up. A pin ended by death leaves the pose - and the victim's
		// lowered view - exactly where it was: the body is already hidden and is destroyed when
		// spectating starts, and restoring it would pop the dying player's camera back up to
		// standing height during the death beat. IsAlive() is reliable here in both orders: on a
		// client, health and the pin arrive in the same update, and every replicated value is
		// applied before any RepNotify runs.
		if (IsAlive())
		{
			ClearPinnedPose();
		}

		// Only a living player gets movement back. On death the restraint ends from inside
		// HandleDeath, after movement was disabled for good, and this must not undo that.
		if (Movement && IsAlive())
		{
			Movement->SetDefaultMovementMode();
		}

		if (AController* InputController = RestraintInputController.Get())
		{
			InputController->SetIgnoreMoveInput(false);
		}
		RestraintInputController.Reset();

		if (IsLocallyControlled() && IsAlive())
		{
			DinoScreenLog(TEXT("You're free - run."), FColor(120, 220, 120), 4.0f);
		}

		OnRestraintEnded();
	}
}

// --- Pinned pose -----------------------------------------------------------------------------

void ADinoCharacter::ApplyPinnedPose()
{
	USkeletalMeshComponent* Body = GetMesh();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Body || !Capsule)
	{
		return;
	}

	// Feet toward whatever has them, as it was when it caught them - see FDinoRestraintState.
	// Horizontal, so the body lies flat even when the raptor caught them from a little higher up.
	FVector ToCaptor = FVector(Restraint.Direction).GetSafeNormal2D();
	if (ToCaptor.IsNearlyZero())
	{
		ToCaptor = GetActorForwardVector().GetSafeNormal2D();
	}

	// A first-person character turns its whole actor with the mouse. Left on, a pinned player
	// looking around would spin their own body on the ground. Every machine runs this, so the
	// server, the victim and anyone watching agree; ClearPinnedPose restores it.
	bUseControllerRotationYaw = false;

	// The mannequin stands along its local +Z with its face along local +Y. Lying on its back
	// with its feet to the captor means local +Z (feet to head) points away from the captor, and
	// local +Y (the face) points up.
	const FRotator Lying = FRotationMatrix::MakeFromYZ(FVector::UpVector, -ToCaptor).Rotator();

	// The mesh's origin is at its feet. Placing the feet half a body-length toward the captor
	// centres the lying body on the capsule, under the raptor rather than beside it.
	const FVector Ground = GetActorLocation() - FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight());
	const FVector Feet = Ground + ToCaptor * (PinnedBodyLength * 0.5f) + FVector(0.0f, 0.0f, PinnedBodyLift);

	// World space, so it does not matter what the capsule or a crouch did to the mesh's
	// relative offset - ClearPinnedPose restores that from ACharacter's own cached baseline.
	Body->SetWorldLocationAndRotation(Feet, Lying);
	bPinnedPoseApplied = true;

	if (!IsLocallyControlled())
	{
		return;
	}

	const UCameraComponent* Camera = FindComponentByClass<UCameraComponent>();
	FVector ViewFrom = Camera ? Camera->GetComponentLocation() : GetPawnViewLocation();

	// The victim's own view: the camera rides the first-person arms (on their head socket), so
	// moving the arms moves the camera. Measured from where they actually are now rather than
	// from guessed bone heights, so it holds if the arms or camera are ever moved.
	UPrimitiveComponent* Arms = bLowerViewWhenPinned ? FindFirstPersonMesh() : nullptr;
	if (Arms && Camera)
	{
		if (!PosedFirstPersonMesh.IsValid())
		{
			SavedFirstPersonMeshTransform = Arms->GetRelativeTransform();
			PosedFirstPersonMesh = Arms;
		}

		// Eyes sit a hand's width short of the crown, at the lying head's height off the floor.
		const FVector Eyes = Ground - ToCaptor * (PinnedBodyLength * 0.5f - 15.0f) + FVector(0.0f, 0.0f, PinnedEyeHeight);
		const FVector CameraFromArms = Camera->GetComponentLocation() - Arms->GetComponentLocation();
		Arms->SetWorldLocation(Eyes - CameraFromArms);
		ViewFrom = Eyes;

		// Hidden rather than left hovering at floor level. The camera stays attached and keeps
		// working - hiding a component does not stop its sockets updating.
		Arms->SetHiddenInGame(true);
	}

	// Turn the view onto the raptor and hold it there, with a little room to look around. Aimed
	// at the spot the raptor is moving to, at its eye height, rather than at the raptor itself:
	// at this moment it may still be mid-pounce, and on a client its move may not have arrived.
	if (ADinoPlayerController* PC = Cast<ADinoPlayerController>(GetController()))
	{
		// A fallback for a captor not yet resolved on this client; about a raptor's eye height.
		constexpr float FallbackCaptorEyeHeight = 120.0f;
		const ADinoCreature* Captor = Restraint.Captor;
		const float CaptorEyeHeight = Captor ? Captor->GetEyeHeightAboveFeet() : FallbackCaptorEyeHeight;
		const FVector LookAt = GetPinnedCaptorSpot() + FVector(0.0f, 0.0f, CaptorEyeHeight);

		PC->ConstrainView((LookAt - ViewFrom).Rotation(), PinnedLookYawLeeway, PinnedLookPitchLeeway);
	}
}

FVector ADinoCharacter::GetPinnedCaptorSpot() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
	const FVector Ground = GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);

	return Ground + FVector(Restraint.Direction).GetSafeNormal2D() * PinnedCaptorOffset;
}

void ADinoCharacter::ClearPinnedPose()
{
	if (!bPinnedPoseApplied)
	{
		return;
	}
	bPinnedPoseApplied = false;

	// ACharacter caches the mesh's offset from the capsule at startup, and its crouch code
	// works from the same baseline - so restoring to it agrees with whatever crouch does next.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetRelativeLocationAndRotation(GetBaseTranslationOffset(), GetBaseRotationOffset());
	}

	// From this class's defaults, which carry the Blueprint's setting rather than C++'s.
	bUseControllerRotationYaw = GetClass()->GetDefaultObject<ADinoCharacter>()->bUseControllerRotationYaw;

	if (UPrimitiveComponent* Arms = PosedFirstPersonMesh.Get())
	{
		Arms->SetRelativeTransform(SavedFirstPersonMeshTransform);
		Arms->SetHiddenInGame(false);
	}
	PosedFirstPersonMesh.Reset();

	if (IsLocallyControlled())
	{
		if (ADinoPlayerController* PC = Cast<ADinoPlayerController>(GetController()))
		{
			PC->ReleaseViewConstraint();
		}
	}
}

UPrimitiveComponent* ADinoCharacter::FindFirstPersonMesh() const
{
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents(Primitives);

	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive && Primitive->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson
			&& Primitive->IsA<USkeletalMeshComponent>())
		{
			return Primitive;
		}
	}

	return nullptr;
}

void ADinoCharacter::OnRestraintBegan_Implementation(ADinoCreature* Captor, EDinoRestraint Kind)
{
	UE_LOG(LogDinoGame, Log, TEXT("%s restrained (%s) by %s."), *GetName(),
		*UEnum::GetDisplayValueAsText(Kind).ToString(), *GetNameSafe(Captor));
}

void ADinoCharacter::OnRestraintEnded_Implementation()
{
	UE_LOG(LogDinoGame, Log, TEXT("%s released."), *GetName());
}
