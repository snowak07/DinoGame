#include "DinoMenuGameMode.h"

#include "DinoPlayerController.h"

ADinoMenuGameMode::ADinoMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ADinoPlayerController::StaticClass();

	// Hosting from here must be a hard travel: seamless travel reuses the current network
	// context and cannot turn a standalone game into a listen server. See
	// UDinoSessionSubsystem::HandleCreateComplete.
	bUseSeamlessTravel = false;
}

void ADinoMenuGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// Told explicitly rather than having the controller guess from the map or the game state
	// class. A client's controller can begin play before the game state has replicated, and a
	// guess made then would put the main menu over a live session.
	if (ADinoPlayerController* DinoController = Cast<ADinoPlayerController>(NewPlayer))
	{
		DinoController->ClientShowMainMenu();
	}
}
