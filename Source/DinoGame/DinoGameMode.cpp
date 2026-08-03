#include "DinoGameMode.h"

#include "DinoGame.h"
#include "DinoGameState.h"
#include "DinoPlayerController.h"
#include "DinoPlayerState.h"
#include "GameFramework/PlayerController.h"

ADinoGameMode::ADinoGameMode()
{
	GameStateClass = ADinoGameState::StaticClass();
	PlayerStateClass = ADinoPlayerState::StaticClass();
	PlayerControllerClass = ADinoPlayerController::StaticClass();

	// Keeps PlayerStates and their replicated data alive across level changes, so a co-op run
	// can move between maps without every player being torn down and rebuilt.
	bUseSeamlessTravel = true;
}

void ADinoGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UE_LOG(LogDinoNet, Log, TEXT("Player joined. Total players: %d"), GetNumPlayers());
	OnPlayerCountChanged(GetNumPlayers());
}

void ADinoGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	UE_LOG(LogDinoNet, Log, TEXT("Player left. Total players: %d"), GetNumPlayers());
	OnPlayerCountChanged(GetNumPlayers());
}
