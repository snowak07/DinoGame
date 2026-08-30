#include "DinoGame.h"

#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, DinoGame, "DinoGame" );

DEFINE_LOG_CATEGORY(LogDinoGame)
DEFINE_LOG_CATEGORY(LogDinoNet)

void DinoScreenLog(const FString& Message, const FColor& Colour, float Duration)
{
	UE_LOG(LogDinoNet, Log, TEXT("%s"), *Message);

	if (GEngine)
	{
		// INDEX_NONE means "do not replace an existing message", so successive lines stack
		// instead of overwriting each other. Multi-line output like DinoNetStatus depends on it.
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, Duration, Colour, Message);
	}
}

void DinoScreenError(const FString& Message)
{
	UE_LOG(LogDinoNet, Error, TEXT("%s"), *Message);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 30.0f, FColor::Red, Message);
	}
}
