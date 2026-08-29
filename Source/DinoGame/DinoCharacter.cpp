#include "DinoCharacter.h"

#include "Components/SkeletalMeshComponent.h"
#include "DinoGame.h"
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

	if (CurrentHealth <= 0.0f && OldHealth > 0.0f)
	{
		UE_LOG(LogDinoGame, Log, TEXT("%s died."), *GetName());
		OnDied.Broadcast();
	}
}
