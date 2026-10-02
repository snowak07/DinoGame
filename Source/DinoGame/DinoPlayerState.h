#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "DinoPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDinoAliveStateChanged, bool, bIsAlive);

UCLASS()
class DINOGAME_API ADinoPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** True only while this player has a living character in the current round. */
	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	bool IsAlive() const { return bIsAlive; }

	/** Server only. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Multiplayer")
	void SetIsAlive(bool bNewIsAlive);

	/** Whether this player is the one hosting. Replicated so every lobby can mark them. */
	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	bool IsHost() const { return bIsHost; }

	/** Server only. Set once, at login. */
	void SetIsHost(bool bNewIsHost);

	/** Fires on every machine, including the server, whenever the alive state changes. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Multiplayer")
	FDinoAliveStateChanged OnAliveStateChanged;

protected:
	/**
	 * False by default, and set true only when the game mode spawns a character for a round.
	 *
	 * Defaulting to true would count lobby players and late joiners - neither of whom has a
	 * character - as alive, and a round whose end condition is "nobody alive" could then never
	 * end. "Alive" has to mean "has a living character right now", nothing looser.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsAlive, BlueprintReadOnly, Category = "Dino|Multiplayer")
	bool bIsAlive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dino|Multiplayer")
	bool bIsHost = false;

	UFUNCTION()
	void OnRep_IsAlive();
};
