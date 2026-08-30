#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDinoGame, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDinoNet, Log, All);

/**
 * Writes a line to the log and mirrors it on screen.
 *
 * Console commands that only UE_LOG are invisible on a machine that was not launched with
 * -log, which is every machine running the game from the itch app. Anything a player is
 * expected to read as the result of a command should go through here instead.
 *
 * Screen messages are compiled out of Shipping builds; the log half always happens.
 */
void DinoScreenLog(const FString& Message, const FColor& Colour = FColor::White, float Duration = 20.0f);

/** Red, and longer-lived, since a failure is the thing you most need time to read. */
void DinoScreenError(const FString& Message);
