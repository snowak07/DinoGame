#pragma once

#include "CoreMinimal.h"

/**
 * The tuning behind going down and being revived (dino.Downed.*), read live so it can be tuned
 * mid-session. Host. The rules that use it are DinoHealthRules.
 */
namespace DinoDowned
{
	/** How long a full bleed-out meter lasts, untouched. */
	float BleedOutSeconds();

	/** How long a revive takes, holding the key throughout. */
	float ReviveSeconds();

	/** How close (cm, centre to centre) a reviver has to stay. */
	float ReviveRange();

	/** The share of max health a revived player stands back up with. */
	float ReviveHealthFraction();

	/** How long a raptor shot off its victim leaves that victim alone. */
	float FlaredVictimIgnoreSeconds();
}
