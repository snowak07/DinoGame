#include "DinoDowned.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarBleedOutSeconds(TEXT("dino.Downed.BleedOutSeconds"), 45.0f,
		TEXT("How long (s) a downed player lasts untouched before bleeding out. Damage while down shortens it. Default 45. Host."));

	TAutoConsoleVariable<float> CVarReviveSeconds(TEXT("dino.Downed.ReviveSeconds"), 4.0f,
		TEXT("How long (s) a teammate holds the revive key to stand a downed player up. Default 4. Host."));

	TAutoConsoleVariable<float> CVarReviveRange(TEXT("dino.Downed.ReviveRange"), 200.0f,
		TEXT("How close (cm) a reviver has to stay to the downed player. Default 200. Host."));

	TAutoConsoleVariable<float> CVarReviveHealthFraction(TEXT("dino.Downed.ReviveHealth"), 0.2f,
		TEXT("Share of max health a revived player stands up with. Default 0.2. Host."));

	TAutoConsoleVariable<float> CVarFlaredVictimIgnoreSeconds(TEXT("dino.AI.FlaredVictimIgnoreSeconds"), 10.0f,
		TEXT("How long (s) a raptor shot off its victim leaves that victim alone, so a revive can happen. Default 10. Host."));
}

float DinoDowned::BleedOutSeconds()           { return CVarBleedOutSeconds.GetValueOnGameThread(); }
float DinoDowned::ReviveSeconds()             { return CVarReviveSeconds.GetValueOnGameThread(); }
float DinoDowned::ReviveRange()               { return CVarReviveRange.GetValueOnGameThread(); }
float DinoDowned::ReviveHealthFraction()      { return CVarReviveHealthFraction.GetValueOnGameThread(); }
float DinoDowned::FlaredVictimIgnoreSeconds() { return CVarFlaredVictimIgnoreSeconds.GetValueOnGameThread(); }
