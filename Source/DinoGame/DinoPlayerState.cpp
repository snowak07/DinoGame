#include "DinoPlayerState.h"

#include "Net/UnrealNetwork.h"

void ADinoPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADinoPlayerState, bIsAlive);
}

void ADinoPlayerState::SetIsAlive(bool bNewIsAlive)
{
	if (!HasAuthority() || bIsAlive == bNewIsAlive)
	{
		return;
	}

	bIsAlive = bNewIsAlive;

	// RepNotifies do not fire on the authority that set the value, so broadcast here to keep
	// listen-server and client behaviour identical.
	OnAliveStateChanged.Broadcast(bIsAlive);
}

void ADinoPlayerState::OnRep_IsAlive()
{
	OnAliveStateChanged.Broadcast(bIsAlive);
}
