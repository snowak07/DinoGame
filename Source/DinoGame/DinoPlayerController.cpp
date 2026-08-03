#include "DinoPlayerController.h"

void ADinoPlayerController::AcknowledgePossession(APawn* NewPawn)
{
	Super::AcknowledgePossession(NewPawn);

	OnLocalPawnReady(NewPawn);
}
