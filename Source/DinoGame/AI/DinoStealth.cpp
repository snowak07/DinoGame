#include "AI/DinoStealth.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarCrouchSightFactor(TEXT("dino.Stealth.CrouchSightFactor"), 0.6f,
		TEXT("A crouched player is seen only within this fraction of a creature's sight range. Default 0.6. Host."));

	TAutoConsoleVariable<float> CVarCrouchCloseSenseFactor(TEXT("dino.Stealth.CrouchCloseSenseFactor"), 0.5f,
		TEXT("A crouched player is felt only within this fraction of a creature's close-sense radius (3 m). Default 0.5. Host."));

	TAutoConsoleVariable<float> CVarFootstepRange(TEXT("dino.Noise.FootstepRange"), 600.0f,
		TEXT("How far (cm) walking at full speed carries to a creature; slower is quieter, crouch-walking silent. Default 600 (6 m). Host."));

	TAutoConsoleVariable<float> CVarLandingRange(TEXT("dino.Noise.LandingRange"), 1000.0f,
		TEXT("How far (cm) landing from a jump or a fall carries to a creature. Default 1000 (10 m). Host."));
}

float DinoStealth::CrouchSightFactor()      { return CVarCrouchSightFactor.GetValueOnGameThread(); }
float DinoStealth::CrouchCloseSenseFactor() { return CVarCrouchCloseSenseFactor.GetValueOnGameThread(); }
float DinoStealth::FootstepRange()          { return CVarFootstepRange.GetValueOnGameThread(); }
float DinoStealth::LandingRange()           { return CVarLandingRange.GetValueOnGameThread(); }
