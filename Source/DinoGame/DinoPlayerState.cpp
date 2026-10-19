#include "DinoPlayerState.h"

#include "DinoCharacter.h"
#include "DinoDamageTypes.h"
#include "DinoGame.h"
#include "DinoGameState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Online/DinoVoiceTalker.h"

ADinoPlayerState::ADinoPlayerState()
{
	VoiceTalker = CreateDefaultSubobject<UDinoVoiceTalker>(TEXT("VoiceTalker"));
	VoiceTalker->SetOwningPlayer(this);
}

void ADinoPlayerState::BeginPlay()
{
	Super::BeginPlay();

	VoiceTalker->SetOwningPlayer(this);
	RegisterVoiceTalker();
	RefreshVoicePlacement();
}

void ADinoPlayerState::OnSetUniqueId()
{
	Super::OnSetUniqueId();

	RegisterVoiceTalker();
}

void ADinoPlayerState::RegisterVoiceTalker()
{
	if (VoiceTalker && GetUniqueId().IsValid())
	{
		VoiceTalker->Register(this);
	}
}

void ADinoPlayerState::SetVoiceBody(ADinoCharacter* Body)
{
	VoiceBody = Body;
	RefreshVoicePlacement();
}

void ADinoPlayerState::ClearVoiceBody(ADinoCharacter* Body)
{
	if (VoiceBody.Get() == Body)
	{
		VoiceBody.Reset();
		RefreshVoicePlacement();
	}
}

void ADinoPlayerState::RefreshVoicePlacement()
{
	if (!VoiceTalker)
	{
		return;
	}

	ADinoCharacter* Body = VoiceBody.Get();
	const bool bFromBody = IsAlive() && IsValid(Body) && Body->IsAlive();

	VoiceTalker->SetPlacement(bFromBody ? Body->GetRootComponent() : nullptr,
		bFromBody ? Body->GetVoiceAttenuation() : nullptr);
}

float ADinoPlayerState::GetVoiceLevel() const
{
	return VoiceTalker ? VoiceTalker->GetVoiceLevel() : 0.0f;
}

void ADinoPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADinoPlayerState, StatusState);
	DOREPLIFETIME(ADinoPlayerState, bIsHost);
}

void ADinoPlayerState::SetStatus(EDinoPlayerStatus NewStatus, TSubclassOf<UDamageType> Cause)
{
	if (!HasAuthority() || StatusState.Status == NewStatus)
	{
		return;
	}

	const EDinoPlayerStatus OldStatus = StatusState.Status;
	StatusState.Status = NewStatus;
	StatusState.Cause = NewStatus == EDinoPlayerStatus::Dead ? Cause : nullptr;

	ForceNetUpdate();

	// RepNotifies do not fire on the machine that set the value, so apply here to keep the
	// listen-server host and its clients behaving identically.
	ApplyStatusChange(OldStatus);
}

void ADinoPlayerState::SetIsHost(bool bNewIsHost)
{
	if (HasAuthority())
	{
		bIsHost = bNewIsHost;
	}
}

void ADinoPlayerState::OnRep_StatusState(const FDinoPlayerStatusState& OldState)
{
	if (OldState.Status != StatusState.Status)
	{
		ApplyStatusChange(OldState.Status);
	}
}

void ADinoPlayerState::ApplyStatusChange(EDinoPlayerStatus OldStatus)
{
	const EDinoPlayerStatus NewStatus = StatusState.Status;

	UE_LOG(LogDinoNet, Log, TEXT("%s: %s -> %s%s"), *GetPlayerName(),
		*UEnum::GetDisplayValueAsText(OldStatus).ToString(),
		*UEnum::GetDisplayValueAsText(NewStatus).ToString(),
		NewStatus == EDinoPlayerStatus::Dead
			? *FString::Printf(TEXT(" (%s)"), *UDinoDamageType::NameOf(StatusState.Cause))
			: TEXT(""));

	// Before the broadcast, so anything reacting to the change already hears this player from
	// the right place - or, once dead, from nowhere.
	RefreshVoicePlacement();

	OnStatusChanged.Broadcast(this, OldStatus, NewStatus);

	// On a client this PlayerState can arrive before the game state does, in which case the relay
	// is skipped. That is fine: listeners rebuild from current statuses when they bind, and when
	// the game state reports a new player (ADinoGameState::OnPlayerListChanged).
	if (ADinoGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ADinoGameState>() : nullptr)
	{
		GameState->NotifyPlayerStatusChanged(this, OldStatus, NewStatus);
	}
}
