#include "DinoGameState.h"

#include "DinoPlayerState.h"
#include "GameFramework/GameMode.h"
#include "Net/UnrealNetwork.h"

void ADinoGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADinoGameState, JoinCode);
}

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

bool ADinoGameState::IsInLobby() const
{
	return GetMatchState() == MatchState::WaitingToStart;
}

bool ADinoGameState::IsRoundInProgress() const
{
	return GetMatchState() == MatchState::InProgress;
}

bool ADinoGameState::IsRoundOver() const
{
	return GetMatchState() == MatchState::WaitingPostMatch;
}

void ADinoGameState::SetJoinCode(const FString& NewJoinCode)
{
	if (HasAuthority())
	{
		JoinCode = NewJoinCode;
	}
}
