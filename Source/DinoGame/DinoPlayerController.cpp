#include "DinoPlayerController.h"

#include "DinoBuildInfo.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/VoiceInterface.h"
#include "Online/DinoSessionSubsystem.h"
#include "OnlineSubsystem.h"
#include "UI/DinoJoinMenu.h"

void ADinoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Covers the listen-server host and standalone, where no voice handshake happens.
	EnableOpenMic();

	if (IsLocalController())
	{
		UE_LOG(LogDinoGame, Log, TEXT("Build version: %s"), *UDinoBuildInfo::GetBuildVersion());
		ShowBuildVersionOnScreen();
	}
}

void ADinoPlayerController::ShowBuildVersionOnScreen()
{
	if (!GEngine)
	{
		return;
	}

	// A stable key so repeat calls replace the message rather than stacking copies.
	static const int32 VersionMessageKey = 20260828;

	// Effectively forever. AddOnScreenDebugMessage has no "never expire" value, and the
	// message is cleared on level load anyway, so this only has to outlast one session.
	static const float PersistentDuration = 1.0e9f;

	const bool bStamped = UDinoBuildInfo::IsStampedBuild();
	const FString Message = FString::Printf(TEXT("Build %s"), *UDinoBuildInfo::GetBuildVersion());

	// Unstamped means an editor or hand-built copy, which tells a tester nothing useful about
	// which package they are on — colour it differently so that is obvious at a glance.
	GEngine->AddOnScreenDebugMessage(
		VersionMessageKey,
		PersistentDuration,
		bStamped ? FColor::Silver : FColor::Orange,
		Message);
}

void ADinoPlayerController::DinoBuildVersion()
{
	UE_LOG(LogDinoGame, Log, TEXT("Build version: %s"), *UDinoBuildInfo::GetBuildVersion());
	ShowBuildVersionOnScreen();
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

void ADinoPlayerController::ToggleJoinMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	if (JoinMenu && JoinMenu->IsInViewport())
	{
		JoinMenu->HideMenu();
		return;
	}

	if (!JoinMenuClass)
	{
		DinoScreenError(TEXT("No join menu set. Assign JoinMenuClass on BP_FirstPersonPlayerController."));
		return;
	}

	if (!JoinMenu)
	{
		JoinMenu = CreateWidget<UDinoJoinMenu>(this, JoinMenuClass);
	}

	if (JoinMenu)
	{
		JoinMenu->ShowMenu();
	}
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

void ADinoPlayerController::DinoMenu()
{
	ToggleJoinMenu();
}

void ADinoPlayerController::DinoCode()
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		const FString Code = Subsystem->GetCurrentJoinCode();
		if (Code.IsEmpty())
		{
			DinoScreenError(TEXT("Not hosting, so there is no join code. Run DinoHost first."));
			return;
		}

		DinoScreenLog(FString::Printf(TEXT("JOIN CODE: %s"), *Code), FColor::Yellow, 300.0f);
	}
}

void ADinoPlayerController::DinoJoinCode(const FString& JoinCode)
{
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->JoinByCode(JoinCode);
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

	const float StatusDuration = 60.0f;

	DinoScreenLog(TEXT("--- Dino voice status ---"), FColor::Cyan, StatusDuration);
	DinoScreenLog(FString::Printf(TEXT("  [Voice].bEnabled : %s"), bVoiceEnabled ? TEXT("true") : TEXT("false")), FColor::White, StatusDuration);

	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	const IOnlineVoicePtr Voice = Subsystem ? Subsystem->GetVoiceInterface() : nullptr;
	if (!Voice.IsValid())
	{
		// Almost always [Voice].bEnabled being false, or no online subsystem at all.
		DinoScreenError(TEXT("  voice interface : <none> - voice is off or unavailable."));
		return;
	}

	DinoScreenLog(FString::Printf(TEXT("  local talkers   : %d"), Voice->GetNumLocalTalkers()), FColor::White, StatusDuration);
	DinoScreenLog(FString::Printf(TEXT("  speaking now    : %s"), Voice->IsLocalPlayerTalking(0) ? TEXT("yes") : TEXT("no")), FColor::White, StatusDuration);
	DinoScreenLog(FString::Printf(TEXT("  headset present : %s"), Voice->IsHeadsetPresent(0) ? TEXT("yes") : TEXT("no")), FColor::White, StatusDuration);

	// There is no remote-talker count on the interface, so walk the PlayerStates instead. This
	// also distinguishes "peer is connected but silent" from "peer never registered", which a
	// bare count would hide.
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (GameState)
	{
		DinoScreenLog(FString::Printf(TEXT("  remote players  : %d"), GameState->PlayerArray.Num() - 1), FColor::White, StatusDuration);
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (!Other || Other == PlayerState)
			{
				continue;
			}

			const FUniqueNetIdRepl& OtherId = Other->GetUniqueId();
			const bool bTalking = OtherId.IsValid() && Voice->IsRemotePlayerTalking(*OtherId);
			DinoScreenLog(FString::Printf(TEXT("    %-20s %s"), *Other->GetPlayerName(),
				!OtherId.IsValid() ? TEXT("<no net id>") : (bTalking ? TEXT("talking") : TEXT("silent"))), FColor::White, StatusDuration);
		}
	}

	if (const ADinoCharacter* DinoPawn = Cast<ADinoCharacter>(GetPawn()))
	{
		DinoScreenLog(FString::Printf(TEXT("  our voice level : %.3f"), DinoPawn->GetVoiceLevel()), FColor::White, StatusDuration);
	}
	else
	{
		DinoScreenLog(TEXT("  our voice level : <no DinoCharacter possessed>"), FColor::White, StatusDuration);
	}

	// The engine's own dump stays log-only: it is dozens of lines and would bury everything
	// above it on screen. Read it from Saved/Logs when the summary above is not enough.
	UE_LOG(LogDinoNet, Log, TEXT("  engine dump:\n%s"), *Voice->GetVoiceDebugState());
}
