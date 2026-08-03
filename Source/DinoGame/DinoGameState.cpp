#include "DinoGameState.h"

#include "DinoPlayerState.h"

int32 ADinoGameState::GetAlivePlayerCount() const
{
	int32 Count = 0;
	for (const APlayerState* State : PlayerArray)
	{
		if (const ADinoPlayerState* DinoState = Cast<ADinoPlayerState>(State))
		{
			Count += DinoState->IsAlive() ? 1 : 0;
		}
	}
	return Count;
}
