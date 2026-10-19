#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "DinoPlayerState.h"
#include "DinoGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDinoPlayerListChanged);

/**
 * Replicated round state, read by every machine's UI.
 *
 * AGameState rather than AGameStateBase because ADinoGameMode is an AGameMode, and the engine
 * requires the two to match: AGameMode drives the match state, and only AGameState carries and
 * replicates it. Mixing an AGameMode with an AGameStateBase logs an error and the match state
 * never reaches clients.
 *
 * Phases map onto the engine's match states:
 *   WaitingToStart   - the lobby
 *   InProgress       - a round
 *   WaitingPostMatch - round over, everyone dead
 */
UCLASS()
class DINOGAME_API ADinoGameState : public AGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Player status relay ---------------------------------------------------------------
	// One place to listen for every player's status, on every machine. Bind once per map: the
	// game state is replaced on every map load, seamless restarts included.
	//
	// The rule that keeps listeners correct: rebuild from the current statuses (loop PlayerArray
	// and read GetStatus) rather than applying the single change each event describes. Bind
	// both events, and do one rebuild straight after binding - a listener that binds late, or a
	// player who joins late, never sees the changes that came before.

	/** A player's status changed. Every machine, server included. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Player")
	FDinoPlayerStatusChanged OnPlayerStatusChanged;

	/** A player joined or left PlayerArray. Every machine. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Player")
	FDinoPlayerListChanged OnPlayerListChanged;

	/** Called by ADinoPlayerState whenever its status changes. Not for anything else. */
	void NotifyPlayerStatusChanged(ADinoPlayerState* Player, EDinoPlayerStatus OldStatus, EDinoPlayerStatus NewStatus);

	virtual void AddPlayerState(APlayerState* PlayerState) override;
	virtual void RemovePlayerState(APlayerState* PlayerState) override;

	/** Players still in the round - Alive or Downed. Zero in the lobby by definition. */
	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	int32 GetAlivePlayerCount() const;

	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	bool IsInLobby() const;

	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	bool IsRoundInProgress() const;

	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	bool IsRoundOver() const;

	/**
	 * The session's join code, empty when not hosting a session (solo PIE).
	 *
	 * Replicated so every lobby shows it, not only the host's. A client knows the code it typed,
	 * but nothing in its own session subsystem records it, and a player who wants to invite a
	 * third friend should not have to ask the host.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	FString GetJoinCode() const { return JoinCode; }

	/** Server only. */
	void SetJoinCode(const FString& NewJoinCode);

protected:
	UPROPERTY(Replicated)
	FString JoinCode;
};
