#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DinoVoiceSubsystem.generated.h"

class ADinoPlayerState;
class APlayerController;
class IVoiceCapture;

/**
 * This machine's voice settings and microphone. Everything here is local: what you hear and
 * what your own mic does, never anyone else's.
 *
 * - Per-player volume and mute, from the Tab menu. Volume is applied to that speaker's audio
 *   (UDinoVoiceTalker); mute asks the server to stop sending you their voice
 *   (APlayerController::ServerMutePlayer, Unreal's player mute - separate from the gameplay mute
 *   ADinoGameMode applies to dead players, so neither can undo the other).
 * - Mic sensitivity: the level speech must reach before it is sent. Saved between sessions.
 * - Microphone recovery. Unreal's Windows voice capture opens a mic once and never again: an
 *   unplugged mic stayed dead, and a changed Windows default input was ignored. This watches
 *   Windows for mic changes and reopens the right one - see HandleMicDevicesChanged.
 *
 * The engine reads its list of mics once, at startup, and can only open a mic from that list.
 * A mic first connected after the game started gets a "restart to use it" notice instead; using
 * it without a restart is the Notion backlog item "Mic capture on AudioCaptureCore".
 */
UCLASS(Config = GameUserSettings)
class DINOGAME_API UDinoVoiceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Per-player ------------------------------------------------------------------------

	/** 0-2, 1 by default. */
	float GetPlayerVolume(const FUniqueNetIdRepl& PlayerId) const;
	void SetPlayerVolume(ADinoPlayerState* Player, float Volume);

	bool IsPlayerMuted(const FUniqueNetIdRepl& PlayerId) const;
	void SetPlayerMuted(APlayerController* LocalController, ADinoPlayerState* Player, bool bMuted);

	/**
	 * Re-sends every mute to the server. Called whenever the player list changes: after a full
	 * reconnect (as Play In Editor restarts are) the server's mute list starts empty again.
	 */
	void ReapplyMutes(APlayerController* LocalController) const;

	// --- Mic -----------------------------------------------------------------------------------

	/** 0 (needs a loud voice) to 1 (picks up a whisper, and more noise). */
	float GetMicSensitivity() const;
	void SetMicSensitivity(float Sensitivity);

	/** Every mic Windows had when the game started - the only ones the engine can open. */
	const TArray<FString>& GetSelectableMics() const { return StartupMics; }

	/** The mic picked in the Tab menu, or empty to follow the Windows default input. */
	const FString& GetPickedMic() const { return PickedMic; }
	void PickMic(const FString& MicName);

	/** The mic actually open now. Empty before voice has started. */
	const FString& GetActiveMic() const { return ActiveMic; }

	/** Loudest mic level over the last few seconds, 0-1. Speech is sent once it passes the threshold. */
	float GetRecentMicPeak() const;

	/** The level speech has to reach to be sent, from the current sensitivity. */
	float GetMicThreshold() const;

	/** Re-reads the mic list and reopens the right mic now. The Tab menu's Reconnect button. */
	void RefreshMic();

	/** One-time notices, shown once a game world is up (no mic found, and so on). */
	void ShowStartupNotices();

	/** Lines for DinoVoiceStatus. */
	void DescribeMic(TArray<FString>& OutLines) const;

	/** One line on what is wrong with the mic, for the Tab menu. Empty when nothing is. */
	FString GetMicNotice() const;

private:
	bool Tick(float DeltaTime);

	/** Reads the current mic list from Windows, and opens the mic that should be in use. */
	void HandleMicDevicesChanged();

	bool OpenMic(const FString& MicName, IVoiceCapture& Capture);

	void ApplyMicSensitivity() const;

	/** Saved: the mic picked in the Tab menu, empty for the Windows default. */
	UPROPERTY(Config)
	FString PickedMic;

	/** Saved: 0-1, or negative to keep the threshold from DefaultEngine.ini. */
	UPROPERTY(Config)
	float MicSensitivity = -1.0f;

	/** By FUniqueNetIdRepl::ToString(). Session only. */
	TMap<FString, float> PlayerVolumes;
	TSet<FString> MutedPlayers;

	TArray<FString> StartupMics;
	FString ActiveMic;

	/** The capture reported an error - the mic is gone - and has not been reopened since. */
	bool bMicLost = false;

	/**
	 * The engine's mic object at the last check, so a newly opened one is noticed - the first
	 * time voice starts, and if the engine ever replaces it. A new one always opens on the
	 * Windows default, whatever was picked.
	 */
	TWeakPtr<IVoiceCapture> SeenCapture;

	/** The last mic that could not be used because it arrived after startup. */
	FString NewMicNeedingRestart;

	bool bStartupNoticesShown = false;
	bool bMissingMicNotified = false;
	TSet<FString> NewMicsNotified;

	/** Set when Windows reports a device change; acted on after a short settle time. */
	double DevicesChangedAt = -1.0;

	double NextHealthCheck = 0.0;

	struct FLevelSample { double Time; float Level; };
	TArray<FLevelSample> LevelSamples;

	FTSTicker::FDelegateHandle TickHandle;

#if PLATFORM_WINDOWS
	/** Windows' device-change notifications (Core Audio). Only sets a flag; read on the game thread. */
	class FDinoMicWatcher* Watcher = nullptr;
#endif
};
