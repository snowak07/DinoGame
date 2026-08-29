#include "DinoPlayerController.h"

#include "DinoCharacter.h"
#include "DinoGame.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/VoiceInterface.h"
#include "Online/DinoSessionSubsystem.h"
#include "OnlineSubsystem.h"

void ADinoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Covers the listen-server host and standalone, where no voice handshake happens.
	EnableOpenMic();
}

void ADinoPlayerController::ClientVoiceHandshakeComplete()
{
	Super::ClientVoiceHandshakeComplete();

	// Covers remote clients, where the handshake finishes after BeginPlay and transmission
	// started earlier would not stick.
	EnableOpenMic();
}

void ADinoPlayerController::EnableOpenMic()
{
	if (!IsLocalController())
	{
		return;
	}

	StartTalking();
}

void ADinoPlayerController::AcknowledgePossession(APawn* NewPawn)
{
	Super::AcknowledgePossession(NewPawn);

	OnLocalPawnReady(NewPawn);
}

// --- Console scaffolding ---------------------------------------------------------------------
// See the header for why these live here rather than on the subsystem. Delete this block, the
// matching declarations, and UDinoSessionSubsystem's Console* functions together once a real
// join UI exists.

UDinoSessionSubsystem* ADinoPlayerController::GetSessionSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	UDinoSessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UDinoSessionSubsystem>() : nullptr;

	if (!Subsystem)
	{
		UE_LOG(LogDinoNet, Error, TEXT("Session subsystem unavailable."));
	}

	return Subsystem;
}

void ADinoPlayerController::DinoHost()
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		// Empty map name means "the map already loaded"; ConsoleHost resolves it.
		Subsystem->ConsoleHost(FString(), 4);
	}
}

void ADinoPlayerController::DinoHostMap(const FString& MapName, int32 MaxPlayers)
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->ConsoleHost(MapName, MaxPlayers);
	}
}

void ADinoPlayerController::DinoFind()
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->ConsoleFind();
	}
}

void ADinoPlayerController::DinoJoin(int32 SessionIndex)
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->ConsoleJoin(SessionIndex);
	}
}

void ADinoPlayerController::DinoLeave()
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->ConsoleLeave();
	}
}

void ADinoPlayerController::DinoNetStatus()
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->ConsoleNetStatus();
	}
}

void ADinoPlayerController::DinoVoiceStatus()
{
	bool bVoiceEnabled = false;
	GConfig->GetBool(TEXT("Voice"), TEXT("bEnabled"), bVoiceEnabled, GEngineIni);

	UE_LOG(LogDinoNet, Log, TEXT("--- Dino voice status ---"));
	UE_LOG(LogDinoNet, Log, TEXT("  [Voice].bEnabled : %s"), bVoiceEnabled ? TEXT("true") : TEXT("false"));

	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	const IOnlineVoicePtr Voice = Subsystem ? Subsystem->GetVoiceInterface() : nullptr;
	if (!Voice.IsValid())
	{
		// Almost always [Voice].bEnabled being false, or no online subsystem at all.
		UE_LOG(LogDinoNet, Error, TEXT("  voice interface : <none> — voice is off or unavailable."));
		return;
	}

	UE_LOG(LogDinoNet, Log, TEXT("  local talkers   : %d"), Voice->GetNumLocalTalkers());
	UE_LOG(LogDinoNet, Log, TEXT("  speaking now    : %s"), Voice->IsLocalPlayerTalking(0) ? TEXT("yes") : TEXT("no"));
	UE_LOG(LogDinoNet, Log, TEXT("  headset present : %s"), Voice->IsHeadsetPresent(0) ? TEXT("yes") : TEXT("no"));

	// There is no remote-talker count on the interface, so walk the PlayerStates instead. This
	// also distinguishes "peer is connected but silent" from "peer never registered", which a
	// bare count would hide.
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (GameState)
	{
		UE_LOG(LogDinoNet, Log, TEXT("  remote players  : %d"), GameState->PlayerArray.Num() - 1);
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (!Other || Other == PlayerState)
			{
				continue;
			}

			const FUniqueNetIdRepl& OtherId = Other->GetUniqueId();
			const bool bTalking = OtherId.IsValid() && Voice->IsRemotePlayerTalking(*OtherId);
			UE_LOG(LogDinoNet, Log, TEXT("    %-20s %s"), *Other->GetPlayerName(),
				!OtherId.IsValid() ? TEXT("<no net id>") : (bTalking ? TEXT("talking") : TEXT("silent")));
		}
	}

	if (const ADinoCharacter* DinoPawn = Cast<ADinoCharacter>(GetPawn()))
	{
		UE_LOG(LogDinoNet, Log, TEXT("  our voice level : %.3f"), DinoPawn->GetVoiceLevel());
	}
	else
	{
		UE_LOG(LogDinoNet, Log, TEXT("  our voice level : <no DinoCharacter possessed>"));
	}

	// The engine's own dump: per-talker state, which is what tells you whether a remote player is
	// registered but silent versus not registered at all.
	UE_LOG(LogDinoNet, Log, TEXT("  engine dump:\n%s"), *Voice->GetVoiceDebugState());
}
