#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "DinoGameState.generated.h"

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

	/** Players with a living character this round. Zero in the lobby by definition. */
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
