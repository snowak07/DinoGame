#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "DinoGameState.generated.h"

UCLASS()
class DINOGAME_API ADinoGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	int32 GetAlivePlayerCount() const;
};
