#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DinoGameMode.generated.h"

UCLASS()
class DINOGAME_API ADinoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADinoGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

protected:
	/** Fires on the server whenever a player joins or leaves. Player count is post-change. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|Multiplayer")
	void OnPlayerCountChanged(int32 NumPlayers);
};
