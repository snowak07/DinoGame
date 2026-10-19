#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "DinoPlayerState.h"
#include "DinoGameMode.generated.h"

class UDamageType;

/**
 * Runs the round loop: lobby, round, round over, restart.
 *
 * Built on AGameMode rather than AGameModeBase for its match state machine, which maps onto
 * the phases directly - see ADinoGameState for the mapping. bDelayedStart holds every match in
 * the lobby until the host starts it, and AGameMode already withholds pawns until a match is
 * in progress, so the lobby needs no special casing of its own.
 *
 * Every public function here is a host decision. The PlayerController's server RPCs check that
 * the caller is the host before calling them.
 */
UCLASS()
class DINOGAME_API ADinoGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ADinoGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Starts a round from the lobby. Returns false outside the lobby. */
	bool StartRound();

	/** Reloads the map and starts the next round as soon as everyone has arrived. */
	void RestartRound();

	/** Reloads the map into the lobby. */
	void ReturnToLobby();

	/**
	 * Server only. The one way a player dies - everything that kills a player ends up here.
	 *
	 * Lethal damage arrives from ADinoCharacter once its health reaches zero; falling below the
	 * level's Kill Z arrives from ADinoCharacter::FellOutOfWorld; anything new (a kill volume, a
	 * debug command) should call this directly. Safe to call more than once: only the first call
	 * for a living player does anything.
	 *
	 * Steps run in a fixed order, because each depends on the one before:
	 *   1. Status -> Dead, with the cause. Everything after reads it.
	 *   2. The body dies too, if it is not already (a fall has no lethal damage).
	 *   3. Spectating starts after SpectateDelay; the round-end check counts them out at once.
	 *   4. Logged.
	 * Anything else that reacts to a death listens to ADinoGameState::OnPlayerStatusChanged.
	 */
	void KillPlayer(AController* Victim, TSubclassOf<UDamageType> Cause);

	/**
	 * Server only. Who hears whom, rebuilt from scratch from everyone's status:
	 *
	 *   During a round:
	 *     - a living player (Alive or Downed) cannot hear anyone who is not alive - the dead,
	 *       and players who joined mid-round and are spectating;
	 *     - a spectator hears other spectators always, and the living only while their camera
	 *       is following a teammate (ADinoPlayerController::HearsLivingWhileSpectating). Free
	 *       and overhead cameras are for the dead to talk among themselves.
	 *   In the lobby and at round over, everyone hears everyone.
	 *
	 * Done with Unreal's gameplay mute (APlayerController::GameplayMutePlayer), made for exactly
	 * this. Set on the listener's controller here on the server, it stops the server forwarding
	 * the muted speaker's voice to that listener at all (UNetConnection::ShouldReplicateVoicePacketFrom)
	 * and tells the listener's machine to drop it too (ClientMutePlayer). Both only act on a
	 * change, so re-applying every pair each time is cheap. Separate from the mute a player sets
	 * in the Tab menu, which the engine tracks as a different reason.
	 *
	 * Rebuilt rather than updated, per the player status rule: a late joiner, a seamless restart
	 * (which keeps each controller and its mute list) or a missed event cannot leave a stale mute.
	 * Called on any status change, player list change, match phase change, and spectator camera
	 * change.
	 */
	void RefreshVoiceRules();

	/**
	 * GameplayUnmutePlayer, plus the listen-server host's missing unmute (see the definition).
	 * bListenerIsHost: Listener is this machine's own player, whose mute list the server and
	 * client halves share. bListenerMutedThem: they muted this speaker themselves in the Tab
	 * menu, which must stay. Static and public so the unit tests can pin the engine quirk.
	 */
	static void UnmuteForGameplay(APlayerController* Listener, const FUniqueNetIdRepl& SpeakerId, bool bListenerIsHost, bool bListenerMutedThem);

	/** Seconds from death to spectating. Also how long round over waits after the last death. */
	float GetSpectateDelay() const { return SpectateDelay; }

protected:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual bool ReadyToStartMatch_Implementation() override;
	virtual bool ReadyToEndMatch_Implementation() override;
	virtual void HandleMatchHasStarted() override;

	/** Every match phase change - lobby, round, round over - re-applies the voice rules. */
	virtual void OnMatchStateSet() override;

	/**
	 * Delay between dying and the camera leaving the body, so "You died" registers before the
	 * view changes. Round over waits the same, so the last death gets the same beat.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Rounds", meta = (ClampMin = "0.0"))
	float SpectateDelay = 3.0f;

	/**
	 * After a restart, the longest the next round waits for everyone to arrive before starting
	 * without them. A safety net: normally it starts the moment the last player has loaded.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Rounds", meta = (ClampMin = "1.0"))
	float AutoStartTimeout = 20.0f;

	/**
	 * Fires on the server whenever a player joins or leaves. Player count is post-change.
	 *
	 * The parameter is PlayerCount, not NumPlayers: AGameMode has a NumPlayers member, and UHT
	 * refuses a parameter that shadows one.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|Multiplayer")
	void OnPlayerCountChanged(int32 PlayerCount);

private:
	UFUNCTION()
	void HandlePlayerStatusChanged(ADinoPlayerState* ChangedPlayer, EDinoPlayerStatus OldStatus, EDinoPlayerStatus NewStatus);

	UFUNCTION()
	void HandlePlayerListChanged();

	/** The last mute set logged, so the log shows each change once rather than every refresh. */
	FString LastVoiceRules;


	/** Puts a player in the engine's spectating state; the client then spawns its spectator camera. */
	void BeginSpectating(APlayerController* Player);

	/** ServerTravel to the map already loaded. bAutoStartRound skips the lobby on arrival. */
	void ReloadMap(bool bAutoStartRound);

	/**
	 * True only inside HandleMatchHasStarted.
	 *
	 * PlayerCanRestart is the engine's single gate on spawning a player, and it is asked at
	 * other times too: for a late joiner mid-round, and whenever a dead client clicks (the
	 * engine's spectating state requests a respawn on fire). Gating on this flag means
	 * characters are only ever handed out at the start of a round.
	 */
	bool bStartingRound = false;

	/** From the travel URL: start the round on arrival, waiting for this many players. */
	int32 AutoStartPlayerCount = 0;

	/**
	 * When an auto-start stops waiting for stragglers. Set the first time the wait is checked,
	 * not compared against raw world time: seamless travel copies the old world's clock into the
	 * new one (UWorld's seamless travel handler sets LoadedWorld->TimeSeconds from the current
	 * world), so minutes into a session "seconds since this map started" is already past any
	 * timeout the moment the map loads - which started rounds before clients had finished loading.
	 */
	double AutoStartDeadline = -1.0;

	double LastDeathTime = -1.0;
};
