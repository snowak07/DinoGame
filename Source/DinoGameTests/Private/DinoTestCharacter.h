#pragma once

#include "CoreMinimal.h"
#include "DinoCharacter.h"
#include "DinoTestCharacter.generated.h"

/**
 * ADinoCharacter for the bare test world (DinoTestWorld.h), which has no game mode. The engine
 * turns all damage to a pawn away without one (APawn::ShouldTakeDamage), so this opens that one
 * gate; everything else - ADinoCharacter::TakeDamage and the health rules behind it - runs as
 * in the game.
 */
UCLASS(NotBlueprintable, NotPlaceable)
class ADinoTestCharacter : public ADinoCharacter
{
	GENERATED_BODY()

protected:
	virtual bool ShouldTakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) const override
	{
		return Damage != 0.0f;
	}
};
