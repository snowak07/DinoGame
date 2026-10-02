#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DinoMenuGameMode.generated.h"

/**
 * Game mode for the main menu map. Spawns no pawn and tells the player's controller to show
 * the host / join menu.
 *
 * Use it through BP_MenuGameMode, a Blueprint child that sets Player Controller Class to
 * BP_FirstPersonPlayerController. That Blueprint is what knows which widget the menu is
 * (JoinMenuClass) and binds the menu key; this native class cannot name either without
 * hardcoding asset paths.
 *
 * Standalone only - nobody connects to a main menu.
 */
UCLASS()
class DINOGAME_API ADinoMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADinoMenuGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;

protected:
	/** No pawn, ever. The menu covers the screen and there is nothing to walk around in. */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override {}
};
