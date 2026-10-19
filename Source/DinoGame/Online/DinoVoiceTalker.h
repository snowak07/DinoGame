#pragma once

#include "CoreMinimal.h"
#include "Net/VoiceConfig.h"
#include "DinoVoiceTalker.generated.h"

class ADinoPlayerState;
class UAudioComponent;
class USceneComponent;
class USoundAttenuation;

/**
 * One player's voice, as heard on this machine: where it comes from, how loud it is, and the
 * means to make the engine re-apply both.
 *
 * Lives on the player's ADinoPlayerState, not on their character, because the PlayerState
 * exists for the whole session - lobby, alive, dead, spectating. The engine only applies a
 * talker's settings at the moment that speaker's voice starts playing, and only re-applies them
 * after the speaker has been silent long enough to time out. A talker on the character did not
 * exist for a voice that started in the lobby or after death, so that voice stayed global for
 * as long as the speaker kept talking - the "John is heard everywhere" playtest bug.
 *
 * SetPlacement changes where the voice comes from and immediately refreshes it: the current
 * sound player is released (UDinoVoiceCleanupSubsystem::ReleaseSpeakerAudio) and the engine
 * builds a new one, with these settings, from the speaker's next packet.
 *
 * Every machine has one of these per player, and each one only affects what that machine hears.
 */
UCLASS(ClassGroup = (Dino), meta = (DisplayName = "Dino Voice Talker"))
class DINOGAME_API UDinoVoiceTalker : public UVOIPTalker
{
	GENERATED_BODY()

public:
	UDinoVoiceTalker(const FObjectInitializer& ObjectInitializer);

	/** Called by the owning PlayerState when it is created; used to look up per-player volume. */
	void SetOwningPlayer(ADinoPlayerState* InOwner) { OwningPlayer = InOwner; }

	/** Makes this the engine's talker for the owner's net ID. Needs a valid ID. */
	void Register(APlayerState* Owner);

	/** Whether the engine's lookup for this player's ID currently finds this talker. */
	bool IsEngineTalker() const;

	/**
	 * Positions the voice at AttachTo with Attenuation (proximity and occlusion), or plays it
	 * with no position at all when AttachTo is null. A no-op when nothing changed; otherwise the
	 * voice is refreshed so the change is heard from the next word.
	 */
	void SetPlacement(USceneComponent* AttachTo, USoundAttenuation* Attenuation);

	/** Whether the voice currently comes from a body (true) or plays with no position (false). */
	bool IsPositioned() const { return Settings.ComponentToAttachTo != nullptr; }

	/**
	 * Releases the speaker's current sound player so the engine rebuilds it with the current
	 * settings. Nothing to do if they are not talking: the next word picks the settings up anyway.
	 */
	void RefreshVoice();

	/** This machine's volume for this speaker, 0-2. Applied at once if they are talking. */
	void ApplyLocalVolume();

	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;

	/**
	 * The engine's talker lookup holds one talker per net ID, and a talker leaving removes
	 * whatever entry has its ID - even one a newer talker has taken over since. A restart does
	 * exactly that on the server: each player gets a new PlayerState, whose talker registers,
	 * and only then is the old PlayerState destroyed. Its talker then deleted the new one's
	 * entry, and every client's voice played unpositioned, at full volume from anywhere, from
	 * the first restart on. So both ways out let the engine unregister, then put back a newer
	 * talker it wiped out.
	 */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/**
	 * The other way out: a world torn down without its actors being destroyed one by one, where
	 * the base destructor would unregister by ID with the same effect.
	 */
	virtual void BeginDestroy() override;

private:
	/** Unregisters this talker without disturbing a newer one registered under the same ID. */
	void UnregisterKeepingNewer();

	FUniqueNetIdRepl RegisteredId;
	bool bUnregistered = true;
	float LookUpLocalVolume() const;

	TWeakObjectPtr<ADinoPlayerState> OwningPlayer;

	/** The audio component the engine is playing this speaker through right now, if any. */
	TWeakObjectPtr<UAudioComponent> CurrentAudio;
};
