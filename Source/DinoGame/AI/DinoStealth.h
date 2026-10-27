#pragma once

#include "CoreMinimal.h"

/**
 * The tuning behind sneaking: how much crouching hides a player, and how far their movement
 * carries. Console variables (dino.Stealth.*, dino.Noise.Footstep*), read live so they can be
 * tuned mid-session; the rules that use them are DinoTargetRules and DinoNoiseRules.
 */
namespace DinoStealth
{
	/** A crouched player is seen only within this fraction of a creature's sight range. */
	float CrouchSightFactor();

	/** A crouched player is felt only within this fraction of a creature's close-sense radius. */
	float CrouchCloseSenseFactor();

	/** How far (cm) walking at full speed carries. Slower is quieter; crouch-walking is silent. */
	float FootstepRange();

	/** How far (cm) landing from a jump or a fall carries. */
	float LandingRange();
}
