#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "DinoGameMode.generated.h"

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

	/** Called by ADinoCharacter on the server when a player's character dies. */
	void NotifyPlayerDied(AController* Victim);

	/** Seconds from death to spectating. Also how long round over waits after the last death. */
	float GetSpectateDelay() const { return SpectateDelay; }

protected:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual bool ReadyToStartMatch_Implementation() override;
	virtual bool ReadyToEndMatch_Implementation() override;
	virtual void HandleMatchHasStarted() override;

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

	double LastDeathTime = -1.0;
};
