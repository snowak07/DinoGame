#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DinoPlayerController.generated.h"

UCLASS()
class DINOGAME_API ADinoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Called on the owning client once the pawn exists and is possessed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|Multiplayer")
	void OnLocalPawnReady(APawn* NewPawn);

protected:
	virtual void AcknowledgePossession(APawn* NewPawn) override;
};
