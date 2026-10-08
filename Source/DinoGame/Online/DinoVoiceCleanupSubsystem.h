#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DinoVoiceCleanupSubsystem.generated.h"

/**
 * Engine workaround: tears down voice chat's per-speaker audio before a world is destroyed.
 *
 * The engine's voice system gives every remote speaker an audio component registered directly
 * in the current world - not owned by any actor, so the world's own teardown never touches it.
 * It is cleared by FVoiceEngineImpl::OnPostLoadMap, which runs once the *next* map has loaded.
 * A full map load gets away with that ordering. Seamless travel does not: it destroys the old
 * world first, with the components still registered in it. The world logs "Found components
 * to be incorrectly unregistered: VoipListenerSynthComponent", and a moment later the audio
 * thread touches the destroyed world and the game crashes (FAudioDevice::StopSoundsUsingResource).
 *
 * That was the playtest crash on Restart Round, on the client, while the host was talking.
 * Every round restart is a seamless travel, so any restart with someone speaking could hit it.
 *
 * Fix: on FWorldDelegates::OnWorldCleanup - which runs before that check - stop, unregister and
 * destroy every such component in the world being cleaned up, the same steps the engine's own
 * reset takes. The voice system only holds them by weak pointer, so the next packet from that
 * speaker builds a fresh one in the new world, and the engine's late reset finds nothing to do.
 */
UCLASS()
class DINOGAME_API UDinoVoiceCleanupSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	FDelegateHandle WorldCleanupHandle;
};
