#include "Online/DinoVoiceCleanupSubsystem.h"

#include "AudioDevice.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "DinoGame.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "VoipListenerSynthComponent.h"

void UDinoVoiceCleanupSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UDinoVoiceCleanupSubsystem::HandleWorldCleanup);
}

void UDinoVoiceCleanupSubsystem::Deinitialize()
{
	// Quitting tears the subsystems down before the last world is cleaned up, so the world
	// cleanup handler never saw it and the engine's check tripped on every quit. Release here.
	const int32 Released = ReleaseAllSpeakerAudio(nullptr);
	UE_LOG(LogDinoNet, Log, TEXT("Voice: released %d remote speaker component(s) on shutdown."), Released);

	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);

	Super::Deinitialize();
}

void UDinoVoiceCleanupSubsystem::HandleWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	// The delegate is global. In the editor, several PIE instances share it - each one cleans up
	// only the worlds that belong to it.
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	// Logged even at 0, so a log shows this ran at every map change and how many voices were
	// live: proof that the restart-while-talking path (n > 0) was actually exercised.
	const int32 Released = ReleaseAllSpeakerAudio(World);
	UE_LOG(LogDinoNet, Log, TEXT("Voice: released %d remote speaker component(s) before %s was destroyed."),
		Released, *World->GetName());
}

int32 UDinoVoiceCleanupSubsystem::ReleaseAllSpeakerAudio(const UWorld* World)
{
	TArray<UVoipListenerSynthComponent*> Voices;
	for (TObjectIterator<UVoipListenerSynthComponent> It; It; ++It)
	{
		UVoipListenerSynthComponent* Voice = *It;
		if (IsValid(Voice) && (!World || Voice->GetWorld() == World))
		{
			Voices.Add(Voice);
		}
	}

	for (UVoipListenerSynthComponent* Voice : Voices)
	{
		ReleaseSpeakerAudio(Voice);
	}

	return Voices.Num();
}

void UDinoVoiceCleanupSubsystem::FlushAudioThread(FAudioDevice* AudioDevice)
{
	// FlushAudioRenderingCommands belongs to the audio thread: called from the game thread it
	// pumps the mixer's queue there and trips the engine's IsInAudioThread() ensure, a hitch
	// and a crash-report folder at every restart while someone talked. Packaged builds run a
	// separate audio thread, so hop onto it and wait, as FAudioDevice::Flush itself does.
	if (IsInAudioThread())
	{
		AudioDevice->FlushAudioRenderingCommands();
		return;
	}

	FAudioThread::RunCommandOnAudioThread([AudioDevice]()
	{
		AudioDevice->FlushAudioRenderingCommands();
	});

	FAudioCommandFence Fence;
	Fence.BeginFence();
	Fence.Wait();
}

void UDinoVoiceCleanupSubsystem::ReleaseSpeakerAudio(UVoipListenerSynthComponent* Voice)
{
	if (!IsValid(Voice))
	{
		return;
	}

	// Stop first, and let the audio thread catch up, so nothing is still queued to start on
	// this sound when it is destroyed. See the header.
	Voice->Stop();
	if (const UWorld* World = Voice->GetWorld())
	{
		if (FAudioDevice* AudioDevice = World->GetAudioDeviceRaw())
		{
			FlushAudioThread(AudioDevice);
		}
	}

	// The same order as the engine's own FRemoteTalkerDataImpl::Reset: the inner audio
	// component, then the synth itself. Destroyed as well, so the voice system's weak pointer
	// lets go and it builds a new one for the next packet.
	if (UAudioComponent* Audio = Voice->GetAudioComponent())
	{
		if (Audio->IsRegistered())
		{
			Audio->UnregisterComponent();
		}
		Audio->DestroyComponent();
	}

	if (Voice->IsRegistered())
	{
		Voice->UnregisterComponent();
	}
	Voice->DestroyComponent();
}
