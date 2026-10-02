#include "DinoPlayerController.h"

#include "DinoBuildInfo.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "DinoGameMode.h"
#include "DinoGameState.h"
#include "DinoSpectatorPawn.h"
#include "GameFramework/GameMode.h"
#include "TimerManager.h"
#include "UI/DinoSessionMenu.h"
#include "UI/DinoSpectatorOverlay.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/VoiceInterface.h"
#include "Online/DinoSessionSubsystem.h"
#include "OnlineSubsystem.h"
#include "AI/DinoAIControllerBase.h"
#include "AI/DinoAttackComponent.h"
#include "AI/DinoCreature.h"
#include "EngineUtils.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
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

		GetWorldTimerManager().SetTimer(SessionUITimer, this, &ADinoPlayerController::PollSessionUI, 0.25f, true);
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
	if (!IsLocalController() || bOnMainMenu)
	{
		return;
	}

	// In a match there is nothing to host or join - you are already in one. The key becomes the
	// session menu instead.
	if (GetWorld() && GetWorld()->GetGameState<ADinoGameState>())
	{
		if (SessionMenu && SessionMenu->IsOpen())
		{
			CloseSessionMenu();
		}
		else
		{
			OpenSessionMenu();
		}
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

void ADinoPlayerController::ClientShowMainMenu_Implementation()
{
	bOnMainMenu = true;

	// Next tick, not now: this arrives from PostLogin, which can run before the controller has
	// begun play, and a widget created then has no viewport to go into.
	GetWorldTimerManager().SetTimerForNextTick(this, &ADinoPlayerController::ShowMainMenu);
}

void ADinoPlayerController::ShowMainMenu()
{
	if (!JoinMenuClass)
	{
		DinoScreenError(TEXT("No join menu set. Assign JoinMenuClass on BP_FirstPersonPlayerController, ")
			TEXT("and make sure BP_MenuGameMode uses that controller."));
		return;
	}

	if (!JoinMenu)
	{
		JoinMenu = CreateWidget<UDinoJoinMenu>(this, JoinMenuClass);
	}

	if (JoinMenu)
	{
		JoinMenu->SetMainMenuMode(true);
		JoinMenu->ShowMenu();
	}
}

// --- Session UI ------------------------------------------------------------------------------

void ADinoPlayerController::OpenSessionMenu()
{
	if (!SessionMenu)
	{
		SessionMenu = CreateWidget<UDinoSessionMenu>(this, UDinoSessionMenu::StaticClass());
	}

	if (SessionMenu)
	{
		SessionMenu->Open();
	}
}

void ADinoPlayerController::CloseSessionMenu()
{
	if (SessionMenu && SessionMenu->IsOpen())
	{
		SessionMenu->Close();
	}
}

void ADinoPlayerController::PollSessionUI()
{
	const UWorld* World = GetWorld();
	const ADinoGameState* DinoState = World ? World->GetGameState<ADinoGameState>() : nullptr;
	if (bOnMainMenu || !DinoState)
	{
		return;
	}

	const FName CurrentState = DinoState->GetMatchState();
	if (CurrentState != LastSeenMatchState)
	{
		LastSeenMatchState = CurrentState;

		if (CurrentState == MatchState::WaitingToStart || CurrentState == MatchState::WaitingPostMatch)
		{
			OpenSessionMenu();
		}
		else if (CurrentState == MatchState::InProgress)
		{
			CloseSessionMenu();
		}
	}

	// Not in the lobby, where the session menu is already the whole screen.
	const bool bWantOverlay = IsInState(NAME_Spectating)
		&& GetSpectatorPawn() != nullptr
		&& CurrentState != MatchState::WaitingToStart;

	const bool bHasOverlay = SpectatorOverlay && SpectatorOverlay->IsInViewport();
	if (bWantOverlay && !bHasOverlay)
	{
		if (!SpectatorOverlay)
		{
			SpectatorOverlay = CreateWidget<UDinoSpectatorOverlay>(this, UDinoSpectatorOverlay::StaticClass());
		}
		if (SpectatorOverlay)
		{
			SpectatorOverlay->AddToViewport(0);
		}
	}
	else if (!bWantOverlay && bHasOverlay)
	{
		SpectatorOverlay->RemoveFromParent();
	}
}

void ADinoPlayerController::BeginSpectatingState()
{
	Super::BeginSpectatingState();

	if (IsLocalController())
	{
		// Move-ignore is a counter. A restraint raises it and its release lowers it, but dying
		// mid-pin is exactly the case where the pairing is most fragile, and a counter left at
		// one would freeze the spectator camera's fly controls for the rest of the session.
		ResetIgnoreInputFlags();
	}
}

// --- Round requests --------------------------------------------------------------------------

namespace
{
	/**
	 * The host's controller is the only one local to a listen server; every remote player's
	 * controller is not. In standalone the only player is the host.
	 */
	ADinoGameMode* GetGameModeForHost(const APlayerController& Requester, const TCHAR* Action)
	{
		if (!Requester.IsLocalController())
		{
			UE_LOG(LogDinoNet, Warning, TEXT("Rejected %s from %s - only the host can do that."),
				Action, *Requester.GetName());
			return nullptr;
		}

		const UWorld* World = Requester.GetWorld();
		return World ? World->GetAuthGameMode<ADinoGameMode>() : nullptr;
	}
}

void ADinoPlayerController::ServerStartRound_Implementation()
{
	if (ADinoGameMode* GameMode = GetGameModeForHost(*this, TEXT("start round")))
	{
		if (!GameMode->StartRound())
		{
			DinoScreenError(TEXT("A round can only be started from the lobby."));
		}
	}
}

void ADinoPlayerController::ServerRestartRound_Implementation()
{
	if (ADinoGameMode* GameMode = GetGameModeForHost(*this, TEXT("restart round")))
	{
		GameMode->RestartRound();
	}
}

void ADinoPlayerController::ServerReturnToLobby_Implementation()
{
	if (ADinoGameMode* GameMode = GetGameModeForHost(*this, TEXT("return to lobby")))
	{
		GameMode->ReturnToLobby();
	}
}

void ADinoPlayerController::DinoStartRound()
{
	if (!HasAuthority())
	{
		DinoScreenError(TEXT("DinoStartRound only works on the host."));
		return;
	}
	ServerStartRound();
}

void ADinoPlayerController::DinoRestartRound()
{
	if (!HasAuthority())
	{
		DinoScreenError(TEXT("DinoRestartRound only works on the host."));
		return;
	}
	ServerRestartRound();
}

void ADinoPlayerController::DinoLobby()
{
	if (!HasAuthority())
	{
		DinoScreenError(TEXT("DinoLobby only works on the host."));
		return;
	}
	ServerReturnToLobby();
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

void ADinoPlayerController::DinoAIStatus()
{
	const float StatusDuration = 60.0f;

	DinoScreenLog(TEXT("--- Dino AI status ---"), FColor::Cyan, StatusDuration);

	// AI controllers are server-only, so on a client this list is legitimately empty
	// rather than broken. Say so, otherwise it reads as "the AI vanished".
	if (!HasAuthority())
	{
		DinoScreenLog(TEXT("  (client - AI runs on the host, so nothing is listed here)"),
			FColor::Yellow, StatusDuration);
		return;
	}

	int32 Count = 0;
	for (TActorIterator<ADinoAIControllerBase> It(GetWorld()); It; ++It)
	{
		DinoScreenLog(FString::Printf(TEXT("  %s"), *It->DescribeState()), FColor::White, StatusDuration);
		++Count;
	}

	if (Count == 0)
	{
		DinoScreenLog(TEXT("  no AI creatures in the level"), FColor::Yellow, StatusDuration);
	}
}

void ADinoPlayerController::DinoAICheck()
{
	const float Duration = 90.0f;

	DinoScreenLog(TEXT("--- Dino AI setup check ---"), FColor::Cyan, Duration);

	if (!HasAuthority())
	{
		DinoScreenError(TEXT("Run this on the host - AI only exists there."));
		return;
	}

	int32 Count = 0;
	for (TActorIterator<ADinoAIControllerBase> It(GetWorld()); It; ++It)
	{
		TArray<FString> Lines;
		It->RunSetupCheck(Lines);

		for (const FString& Line : Lines)
		{
			const bool bFail = Line.StartsWith(TEXT("[FAIL]"));
			DinoScreenLog(Line, bFail ? FColor::Red : FColor::White, Duration);
		}
		++Count;
	}

	if (Count == 0)
	{
		DinoScreenError(TEXT("No AI creatures found. Is BP_TRex in the level with Auto Possess AI set?"));
	}
}

void ADinoPlayerController::DinoAIHistory()
{
	const float Duration = 120.0f;

	DinoScreenLog(TEXT("--- Dino AI search history (newest first) ---"), FColor::Cyan, Duration);

	if (!HasAuthority())
	{
		DinoScreenError(TEXT("Run this on the host - AI only exists there."));
		return;
	}

	for (TActorIterator<ADinoAIControllerBase> It(GetWorld()); It; ++It)
	{
		TArray<FString> Lines;
		It->DescribeSearchHistory(Lines);
		for (const FString& Line : Lines)
		{
			DinoScreenLog(Line, FColor::White, Duration);
		}
	}
}

void ADinoPlayerController::DinoAIDebug()
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("DinoAI.DebugDraw"));
	if (!CVar)
	{
		DinoScreenError(TEXT("DinoAI.DebugDraw not found."));
		return;
	}

	const bool bEnable = CVar->GetInt() == 0;
	CVar->Set(bEnable ? 1 : 0, ECVF_SetByConsole);

	// Local to this machine: debug draw is per-viewport, so each player toggles their own.
	DinoScreenLog(FString::Printf(TEXT("AI debug draw %s"), bEnable ? TEXT("ON") : TEXT("OFF")),
		FColor::Cyan, 6.0f);
}

void ADinoPlayerController::DinoSetState(const FString& DesiredState)
{
	// Awareness lives on the server; a client forcing it locally would be overwritten by the
	// next replication and look like the command silently failed.
	if (!HasAuthority())
	{
		DinoScreenError(TEXT("DinoSetState only works on the host - AI runs there."));
		return;
	}

	const bool bRelease = DesiredState.StartsWith(TEXT("auto"), ESearchCase::IgnoreCase)
		|| DesiredState.StartsWith(TEXT("release"), ESearchCase::IgnoreCase);

	EDinoAwareness Wanted = EDinoAwareness::Unaware;
	if (!bRelease && !DinoAwarenessFromString(DesiredState, Wanted))
	{
		DinoScreenError(FString::Printf(
			TEXT("Unknown state \"%s\". Try: Unaware, Suspicious, Alerted, Hunting, Searching, att(acking), or auto."),
			*DesiredState));
		return;
	}

	int32 Count = 0;
	for (TActorIterator<ADinoAIControllerBase> It(GetWorld()); It; ++It)
	{
		if (bRelease)
		{
			It->DebugReleaseAwareness();
		}
		else
		{
			It->DebugForceAwareness(Wanted);
		}
		++Count;
	}

	if (Count == 0)
	{
		DinoScreenError(TEXT("No AI creatures in the level."));
	}
}

void ADinoPlayerController::DinoHitDino(float Damage)
{
	// Creature damage is decided where the AI runs. A client applying it locally would change
	// nothing the server knows about, and look like the command silently failed.
	if (!HasAuthority())
	{
		DinoScreenError(TEXT("DinoHitDino only works on the host - creature damage is decided there."));
		return;
	}

	const APawn* Me = GetPawn();
	if (!Me)
	{
		DinoScreenError(TEXT("DinoHitDino needs a possessed pawn to measure from."));
		return;
	}

	ADinoCreature* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ADinoCreature> It(GetWorld()); It; ++It)
	{
		if (It->IsDead())
		{
			continue;
		}

		const float Distance = FVector::Dist(Me->GetActorLocation(), It->GetActorLocation());
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = *It;
		}
	}

	if (!Nearest)
	{
		DinoScreenError(TEXT("No living creatures in the level."));
		return;
	}

	UGameplayStatics::ApplyDamage(Nearest, Damage, this, GetPawn(), UDamageType::StaticClass());

	// Reported after the hit, so the line shows what the hit actually did - staggered, refused,
	// or nothing - rather than what the settings say it should do.
	const UDinoAttackComponent* Attack = Nearest->GetAttackComponent();
	const FString Phase = Attack ? UEnum::GetDisplayValueAsText(Attack->GetPhase()).ToString() : FString(TEXT("no attack"));

	DinoScreenLog(FString::Printf(TEXT("Hit %s for %.0f at %.0f uu -> phase %s%s | %s"),
		*Nearest->GetName(), Damage, NearestDistance, *Phase,
		Nearest->IsDead() ? TEXT(", DEAD") : TEXT(""),
		*Nearest->DescribeVulnerability()),
		FColor(240, 150, 40), 8.0f);
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
