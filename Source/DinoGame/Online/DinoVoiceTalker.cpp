#include "Online/DinoVoiceTalker.h"

#include "Components/AudioComponent.h"
#include "DinoGame.h"
#include "DinoPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Online/DinoVoiceCleanupSubsystem.h"
#include "Online/DinoVoiceSubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "VoipListenerSynthComponent.h"

UDinoVoiceTalker::UDinoVoiceTalker(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDinoVoiceTalker::Register(APlayerState* Owner)
{
	if (!Owner || !Owner->GetUniqueId().IsValid())
	{
		return;
	}

	// RegisterWithPlayerState first unregisters this talker's previous ID. Same ID again (BeginPlay
	// and OnSetUniqueId both call this) would briefly empty the entry, harmlessly.
	RegisterWithPlayerState(Owner);
	RegisteredId = Owner->GetUniqueId();
	bUnregistered = false;

	UE_LOG(LogDinoNet, Log, TEXT("Voice: talker registered for %s."), *Owner->GetPlayerName());
}

bool UDinoVoiceTalker::IsEngineTalker() const
{
	return RegisteredId.IsValid() && UVOIPStatics::GetVOIPTalkerForPlayer(RegisteredId) == this;
}

void UDinoVoiceTalker::UnregisterKeepingNewer()
{
	if (bUnregistered)
	{
		return;
	}
	bUnregistered = true;

	UVOIPTalker* Current = RegisteredId.IsValid() ? UVOIPStatics::GetVOIPTalkerForPlayer(RegisteredId) : nullptr;

	// The base class's own unregister is private; this is its only public route.
	UVOIPTalker::OnComponentDestroyed(false);

	if (Current && Current != this)
	{
		UVOIPStatics::SetVOIPTalkerForPlayer(RegisteredId, Current);
	}
}

void UDinoVoiceTalker::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (bUnregistered)
	{
		Super::OnComponentDestroyed(bDestroyingHierarchy);
		return;
	}

	// Calls Super::OnComponentDestroyed itself, wrapped so a newer talker's entry survives.
	UnregisterKeepingNewer();
}

void UDinoVoiceTalker::BeginDestroy()
{
	UnregisterKeepingNewer();

	Super::BeginDestroy();
}

void UDinoVoiceTalker::SetPlacement(USceneComponent* AttachTo, USoundAttenuation* Attenuation)
{
	// Attenuation is what makes proximity and occlusion happen, and it only applies to a
	// positioned source - so without a body there is nothing for it to do.
	USoundAttenuation* WantedAttenuation = AttachTo ? Attenuation : nullptr;

	if (Settings.ComponentToAttachTo == AttachTo && Settings.AttenuationSettings == WantedAttenuation)
	{
		return;
	}

	Settings.ComponentToAttachTo = AttachTo;
	Settings.AttenuationSettings = WantedAttenuation;

	UE_LOG(LogDinoNet, Log, TEXT("Voice: %s now %s."),
		OwningPlayer.IsValid() ? *OwningPlayer->GetPlayerName() : TEXT("?"),
		AttachTo ? TEXT("positioned at their body") : TEXT("unpositioned"));

	RefreshVoice();
}

void UDinoVoiceTalker::RefreshVoice()
{
	UAudioComponent* Audio = CurrentAudio.Get();
	CurrentAudio.Reset();

	// The engine's sound player for a speaker is a UVoipListenerSynthComponent, and the audio
	// component it hands this talker is that synth's own inner component.
	if (UVoipListenerSynthComponent* Voice = Audio ? Cast<UVoipListenerSynthComponent>(Audio->GetOuter()) : nullptr)
	{
		UDinoVoiceCleanupSubsystem::ReleaseSpeakerAudio(Voice);
	}
}

void UDinoVoiceTalker::OnTalkingBegin(UAudioComponent* AudioComponent)
{
	Super::OnTalkingBegin(AudioComponent);

	CurrentAudio = AudioComponent;
	ApplyLocalVolume();

	// Called by the engine right after it applied this talker's settings to the new sound, so
	// this is the moment to see whether they took.
	FString Detail;
	const bool bMatches = CheckPlayback(Detail);
	const FString Who = OwningPlayer.IsValid() ? OwningPlayer->GetPlayerName() : TEXT("?");
	if (bMatches)
	{
		UE_LOG(LogDinoNet, Log, TEXT("Voice: %s started talking, %s."), *Who, *Detail);
	}
	else
	{
		UE_LOG(LogDinoNet, Warning, TEXT("Voice: %s started talking, but the sound does NOT match its settings: %s."), *Who, *Detail);
	}
}

bool UDinoVoiceTalker::CheckPlayback(FString& OutDetail) const
{
	const UAudioComponent* Audio = CurrentAudio.Get();
	if (!Audio)
	{
		OutDetail = TEXT("not playing");
		return true;
	}

	// ApplyVoiceSettings attaches the synth's inner audio component and puts the attenuation on
	// the synth itself; read both back from where it put them.
	const UVoipListenerSynthComponent* Synth = Cast<UVoipListenerSynthComponent>(Audio->GetOuter());
	const USceneComponent* Parent = Audio->GetAttachParent();
	const USoundAttenuation* Attenuation = Synth ? Synth->AttenuationSettings.Get() : nullptr;

	// Unpositioned, the sound hangs off its own synth: the synth attaches its inner component to
	// itself when it creates it (USynthComponent::CreateAudioComponent), and ApplyVoiceSettings
	// only re-attaches when there is a body to attach to. That, with no attenuation, plays
	// unpositioned at full volume - the intent.
	const bool bOnOwnSynth = Parent == nullptr || Parent == Synth;

	auto Describe = [](const USceneComponent* Component, bool bUnpositioned)
	{
		if (bUnpositioned)
		{
			return FString(TEXT("unpositioned"));
		}
		return FString::Printf(TEXT("attached to %s"), Component->GetOwner() ? *Component->GetOwner()->GetName() : *Component->GetName());
	};

	OutDetail = FString::Printf(TEXT("sound %s with attenuation %s; settings want %s with %s"),
		*Describe(Parent, bOnOwnSynth), Attenuation ? *Attenuation->GetName() : TEXT("none"),
		*Describe(Settings.ComponentToAttachTo, Settings.ComponentToAttachTo == nullptr),
		Settings.AttenuationSettings ? *Settings.AttenuationSettings->GetName() : TEXT("none"));

	const bool bPlacementMatches = Settings.ComponentToAttachTo ? Parent == Settings.ComponentToAttachTo : bOnOwnSynth;
	return bPlacementMatches && Attenuation == Settings.AttenuationSettings;
}

void UDinoVoiceTalker::OnTalkingEnd()
{
	Super::OnTalkingEnd();

	CurrentAudio.Reset();
}

void UDinoVoiceTalker::ApplyLocalVolume()
{
	if (UAudioComponent* Audio = CurrentAudio.Get())
	{
		Audio->SetVolumeMultiplier(LookUpLocalVolume());
	}
}

float UDinoVoiceTalker::LookUpLocalVolume() const
{
	const ADinoPlayerState* Owner = OwningPlayer.Get();
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UDinoVoiceSubsystem* Voice = GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;

	return Owner && Voice ? Voice->GetPlayerVolume(Owner->GetUniqueId()) : 1.0f;
}
