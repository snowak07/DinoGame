#include "Online/DinoVoiceCleanupSubsystem.h"

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

	int32 Removed = 0;
	for (TObjectIterator<UVoipListenerSynthComponent> It; It; ++It)
	{
		UVoipListenerSynthComponent* Voice = *It;
		if (!IsValid(Voice) || Voice->GetWorld() != World)
		{
			continue;
		}

		// The same order as the engine's own FRemoteTalkerDataImpl::Reset: stop, then unregister
		// the inner audio component, then the synth itself. Destroyed as well, so the voice
		// system's weak pointer lets go and it builds a new one for the next packet.
		Voice->Stop();

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

		++Removed;
	}

	if (Removed > 0)
	{
		UE_LOG(LogDinoNet, Log, TEXT("Voice: released %d remote speaker component(s) before %s was destroyed."),
			Removed, *World->GetName());
	}
}
