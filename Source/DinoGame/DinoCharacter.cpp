#include "DinoCharacter.h"

#include "AI/DinoCreature.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DinoGame.h"
#include "DinoPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Net/VoiceConfig.h"
#include "Sound/SoundAttenuation.h"

ADinoCharacter::ADinoCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// The inherited mesh is the third-person body other players see. The owner sees only the
	// first-person arms, which live on the Blueprint.
	GetMesh()->SetOwnerNoSee(true);

	// Adding this to the C++ parent propagates it to BP_FirstPersonCharacter as an inherited
	// component, so the reparented Blueprint picks it up without any editor work.
	VoipTalker = CreateDefaultSubobject<UVOIPTalker>(TEXT("VoipTalker"));
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
	}

	// Covers the listen-server host, whose PlayerState already exists by BeginPlay.
	RegisterVoiceTalker();
}

void ADinoCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server path: the PlayerState is assigned during possession.
	RegisterVoiceTalker();
}

void ADinoCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client path: the PlayerState replicates in separately from the pawn, often after BeginPlay.
	RegisterVoiceTalker();
}

void ADinoCharacter::RegisterVoiceTalker()
{
	APlayerState* OwningState = GetPlayerState();
	if (!VoipTalker || !OwningState)
	{
		return;
	}

	VoipTalker->RegisterWithPlayerState(OwningState);

	// Spatialisation: without ComponentToAttachTo the voice plays unpositioned, and occlusion
	// only applies to spatialised sources — so both of these are needed for either to work.
	VoipTalker->Settings.ComponentToAttachTo = GetRootComponent();
	VoipTalker->Settings.AttenuationSettings = ResolveVoiceAttenuation();

	UE_LOG(LogDinoNet, Verbose, TEXT("Voice talker registered for %s."), *OwningState->GetPlayerName());
}

USoundAttenuation* ADinoCharacter::ResolveVoiceAttenuation()
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
	return VoipTalker ? VoipTalker->GetVoiceLevel() : 0.0f;
}

float ADinoCharacter::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || ActualDamage <= 0.0f || !IsAlive())
	{
		return 0.0f;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - ActualDamage);
	ApplyHealthChange(OldHealth);

	return ActualDamage;
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

	// Living players have to be able to walk through a body, and creatures must not path around
	// one. Stopping creatures *targeting* it is a separate job - perception tracks stimulus
	// sources, not collision - and lives in ADinoAIControllerBase::IsValidTarget.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	if (IsLocallyControlled())
	{
		// The pawn's input only - the host/join menu is bound on the PlayerController, so a dead
		// player can still open it and leave rather than being stuck until someone ends the game.
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			DisableInput(PC);
		}

		// TODO(death-screen): placeholder until there is a real death screen and respawn.
		DinoScreenLog(TEXT("You died."), FColor(220, 50, 50), 30.0f);
	}

	if (HasAuthority())
	{
		// A dead player is no longer held. Released here rather than left to the captor, so the
		// restraint cannot outlive the victim whatever killed them.
		EndRestraint();

		// The first caller SetIsAlive has ever had - until now ADinoGameState::GetAlivePlayerCount
		// has always equalled the player count.
		if (ADinoPlayerState* DinoState = GetPlayerState<ADinoPlayerState>())
		{
			DinoState->SetIsAlive(false);
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
		// On every machine, including the victim's own client. If only the server stopped the
		// movement, the client would keep predicting moves the server then rejects, and a pinned
		// player would jitter against the correction. DisableMovement also blocks jumping, which
		// ignoring move input alone would not.
		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
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

void ADinoCharacter::OnRestraintBegan_Implementation(ADinoCreature* Captor, EDinoRestraint Kind)
{
	UE_LOG(LogDinoGame, Log, TEXT("%s restrained (%s) by %s."), *GetName(),
		*UEnum::GetDisplayValueAsText(Kind).ToString(), *GetNameSafe(Captor));
}

void ADinoCharacter::OnRestraintEnded_Implementation()
{
	UE_LOG(LogDinoGame, Log, TEXT("%s released."), *GetName());
}
