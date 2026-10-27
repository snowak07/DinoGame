#include "DinoPlayerController.h"

#include "DinoBuildInfo.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "DinoGameMode.h"
#include "DinoDamageTypes.h"
#include "DinoGameState.h"
#include "DinoPlayerState.h"
#include "Online/DinoVoiceSubsystem.h"
#include "Online/DinoVoiceTalker.h"
#include "AI/DinoNoiseRules.h"
#include "AI/DinoNoiseSubsystem.h"
#include "DinoSelfCheck.h"
#include "DinoSpectatorPawn.h"
#include "GameFramework/GameMode.h"
#include "Camera/PlayerCameraManager.h"
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

		if (UDinoVoiceSubsystem* VoiceSettings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDinoVoiceSubsystem>() : nullptr)
		{
			VoiceSettings->ShowStartupNotices();
		}

		GetWorldTimerManager().SetTimer(SpeechTimer, this, &ADinoPlayerController::SampleOwnSpeech, 0.25f, true);
	}
}

void ADinoPlayerController::SampleOwnSpeech()
{
	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	const IOnlineVoicePtr Voice = Subsystem ? Subsystem->GetVoiceInterface() : nullptr;
	const UDinoVoiceSubsystem* VoiceSettings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;

	// "Talking" is the voice system's own answer: this player's speech is above their threshold
	// and being sent. Exactly what everyone else hears - and so what a creature should. Read from
	// UDinoVoiceSubsystem, not IOnlineVoice::IsLocalPlayerTalking, which always says no (see there).
	const bool bTalking = Voice.IsValid() && VoiceSettings && VoiceSettings->IsLocallyTalking() && Cast<ADinoCharacter>(GetPawn());
	if (bTalking != bWasReportingSpeech)
	{
		bWasReportingSpeech = bTalking;
		UE_LOG(LogDinoNet, Log, TEXT("Voice noise: %s reporting your voice to the host."), bTalking ? TEXT("started") : TEXT("stopped"));
	}
	if (!bTalking)
	{
		return;
	}

	const float Loudness = DinoNoiseRules::VoiceLoudness(VoiceSettings->GetCurrentMicLevel(),
		VoiceSettings->GetMicThreshold(), UDinoNoiseSubsystem::GetShoutMicLevel());
	ServerReportSpeech(static_cast<uint8>(FMath::RoundToInt(Loudness * 255.0f)));

	DrawOwnVoiceRings(Loudness);
}

void ADinoPlayerController::ServerReportSpeech_Implementation(uint8 Loudness)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - LastSpeechReportTime < 0.2)
	{
		return;
	}
	LastSpeechReportTime = World->GetTimeSeconds();

	// Only a living (or downed) body makes noise. The dead and spectators talk freely.
	ADinoCharacter* Body = Cast<ADinoCharacter>(GetPawn());
	const ADinoPlayerState* Speaker = GetPlayerState<ADinoPlayerState>();
	if (!Body || !Body->IsAlive() || !Speaker || !Speaker->IsAlive())
	{
		return;
	}

	if (UDinoNoiseSubsystem* Noise = World->GetSubsystem<UDinoNoiseSubsystem>())
	{
		Noise->ReportVoice(Body->GetPawnViewLocation(), Loudness / 255.0f, Body);
	}
}

void ADinoPlayerController::DrawOwnVoiceRings(float Loudness)
{
	// The host draws its own; drawing here too would double the rings on the host's screen.
	const UWorld* World = GetWorld();
	if (HasAuthority() || !World || !GetPawn() || !UDinoNoiseSubsystem::IsDebugDrawOn()
		|| !DinoNoiseRules::ShouldDrawVoice(World->GetTimeSeconds(), LastOwnRingTime, LastOwnRingLoudness,
			Loudness, UDinoNoiseSubsystem::GetDebugDrawSeconds()))
	{
		return;
	}

	LastOwnRingTime = World->GetTimeSeconds();
	LastOwnRingLoudness = Loudness;
	UDinoNoiseSubsystem::DrawRangeRings(World, GetPawn(), GetPawn()->GetPawnViewLocation(), Loudness);
}

void ADinoPlayerController::DinoNoise(float Loudness)
{
	const float Clamped = FMath::Clamp(Loudness, 0.0f, 1.0f);
	if (!Cast<ADinoCharacter>(GetPawn()))
	{
		DinoScreenError(TEXT("DinoNoise: you need a living character - start a round first."));
		return;
	}

	DinoScreenLog(FString::Printf(TEXT("Noise: as if you spoke at loudness %.2f (carries %.0f m in the open, less through walls)."),
		Clamped, UDinoNoiseSubsystem::GetOpenRange(Clamped) / 100.0f), FColor(240, 200, 80), 5.0f);
	ServerReportSpeech(static_cast<uint8>(FMath::RoundToInt(Clamped * 255.0f)));

	DrawOwnVoiceRings(Clamped);
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
		return;
	}

	// Restored even with no menu on screen. A seamless restart keeps this controller - and the
	// UI-only input mode and cursor the round-over menu set - while removing the menu itself.
	// Only fixing input when the menu was still showing left players unable to move, with a
	// cursor and no menu, and spectators unable to switch camera.
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

void ADinoPlayerController::HandlePlayerStatusChanged(ADinoPlayerState* ChangedPlayer, EDinoPlayerStatus OldStatus, EDinoPlayerStatus NewStatus)
{
	if (IsLocalController())
	{
		ScheduleSelfCheck(TEXT("status change"));
	}

	const bool bDied = NewStatus == EDinoPlayerStatus::Dead
		&& (OldStatus == EDinoPlayerStatus::Alive || OldStatus == EDinoPlayerStatus::Downed);

	if (!IsLocalController() || !bDied || !ChangedPlayer || ChangedPlayer == PlayerState)
	{
		return;
	}

	DinoScreenLog(FString::Printf(TEXT("%s %s."), *ChangedPlayer->GetPlayerName(),
		*UDinoDamageType::DescribeDeath(ChangedPlayer->GetDeathCause())), FColor(220, 90, 70), 5.0f);
}

void ADinoPlayerController::HandlePlayerListChanged()
{
	if (!IsLocalController())
	{
		return;
	}

	if (const UDinoVoiceSubsystem* VoiceSettings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDinoVoiceSubsystem>() : nullptr)
	{
		VoiceSettings->ReapplyMutes(this);
	}

	ScheduleSelfCheck(TEXT("player list change"));
}

void ADinoPlayerController::ScheduleSelfCheck(const FString& Trigger)
{
	if (!IsLocalController() || !GetWorld())
	{
		return;
	}

	PendingSelfCheckTrigger = Trigger;

	// Past the death beat before spectating (3 s), plus time for replication to land.
	static constexpr float SettleSeconds = 5.0f;
	GetWorldTimerManager().SetTimer(SelfCheckTimer, this, &ADinoPlayerController::RunScheduledSelfCheck, SettleSeconds, false);
}

void ADinoPlayerController::RunScheduledSelfCheck()
{
	const TArray<DinoSelfCheck::FResult> Results = DinoSelfCheck::Run(this, PendingSelfCheckTrigger);
	const int32 Failed = DinoSelfCheck::CountFailures(Results);
	if (Failed > 0)
	{
		// Testers are told, so a broken state is reported the round it happens rather than
		// rediscovered from the log afterwards.
		DinoScreenError(FString::Printf(TEXT("Self-check: %d problem(s) after %s. Run DinoSelfCheck to see them; details are in the log."),
			Failed, *PendingSelfCheckTrigger));
	}
}

void ADinoPlayerController::DinoSelfCheck()
{
	const TArray<DinoSelfCheck::FResult> Results = DinoSelfCheck::Run(this, TEXT("manual"));
	const int32 Failed = DinoSelfCheck::CountFailures(Results);

	const float Duration = 30.0f;
	DinoScreenLog(FString::Printf(TEXT("--- Self-check: %d checks, %d failed (full list in the log) ---"), Results.Num(), Failed),
		Failed > 0 ? FColor::Red : FColor::Green, Duration);
	for (const DinoSelfCheck::FResult& Result : Results)
	{
		if (Result.Outcome == DinoSelfCheck::FResult::EOutcome::Fail)
		{
			DinoScreenLog(FString::Printf(TEXT("  FAIL %s: %s"), *Result.Name, *Result.Detail), FColor::Red, Duration);
		}
	}
}

void ADinoPlayerController::SetSpectatorHearsLiving(bool bHearsLiving)
{
	if (!IsLocalController() || (SentSpectatorHearsLiving.IsSet() && SentSpectatorHearsLiving.GetValue() == bHearsLiving))
	{
		return;
	}

	SentSpectatorHearsLiving = bHearsLiving;
	ServerSetSpectatorHearsLiving(bHearsLiving);
}

void ADinoPlayerController::ServerSetSpectatorHearsLiving_Implementation(bool bHearsLiving)
{
	if (bSpectatorHearsLiving == bHearsLiving)
	{
		return;
	}

	bSpectatorHearsLiving = bHearsLiving;
	if (ADinoGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ADinoGameMode>() : nullptr)
	{
		GameMode->RefreshVoiceRules();
	}
}

void ADinoPlayerController::PawnPendingDestroy(APawn* InPawn)
{
	if (IsInState(NAME_Spectating) && InPawn && InPawn == GetPawn())
	{
		UE_LOG(LogDinoNet, Log, TEXT("Spectating: old body %s destroyed; staying in the spectator camera."), *InPawn->GetName());
		UnPossess();
		return;
	}

	Super::PawnPendingDestroy(InPawn);
}

void ADinoPlayerController::PollSessionUI()
{
	const UWorld* World = GetWorld();
	ADinoGameState* DinoState = World ? World->GetGameState<ADinoGameState>() : nullptr;
	if (bOnMainMenu || !DinoState)
	{
		return;
	}

	if (DinoState != LastSeenGameState.Get())
	{
		LastSeenGameState = DinoState;
		LastSeenMatchState = NAME_None;

		// A new map means a new game state, so the binding is made again each time.
		DinoState->OnPlayerStatusChanged.AddUniqueDynamic(this, &ADinoPlayerController::HandlePlayerStatusChanged);
		DinoState->OnPlayerListChanged.AddUniqueDynamic(this, &ADinoPlayerController::HandlePlayerListChanged);
		HandlePlayerListChanged();
	}

	const FName CurrentState = DinoState->GetMatchState();
	if (CurrentState != LastSeenMatchState)
	{
		LastSeenMatchState = CurrentState;

		ScheduleSelfCheck(FString::Printf(TEXT("match %s"), *CurrentState.ToString()));

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

		// Logged because this step was invisible when joining players' cameras froze.
		UE_LOG(LogDinoNet, Log, TEXT("Spectating: started, camera %s."),
			GetSpectatorPawn() ? *GetSpectatorPawn()->GetName() : TEXT("NOT CREATED"));

		// Dying pinned leaves the view held on the raptor; the spectator camera must not inherit it.
		ReleaseViewConstraint();
	}
}

// --- View constraint -------------------------------------------------------------------------

void ADinoPlayerController::ConstrainView(const FRotator& Centre, float YawLeeway, float PitchLeeway)
{
	if (!IsLocalController() || !PlayerCameraManager)
	{
		return;
	}

	if (!bViewConstrained)
	{
		SavedViewPitchMin = PlayerCameraManager->ViewPitchMin;
		SavedViewPitchMax = PlayerCameraManager->ViewPitchMax;
		SavedViewYawMin = PlayerCameraManager->ViewYawMin;
		SavedViewYawMax = PlayerCameraManager->ViewYawMax;
		bViewConstrained = true;

		SetControlRotation(FRotator(Centre.Pitch, Centre.Yaw, 0.0f));
	}

	// The engine clamps with FMath::ClampAngle, which measures the range from its midpoint and
	// so copes with a window that crosses 0/360 degrees. A full 180 either side would make the
	// range zero wide - hence the cap. Pitch stays short of straight up or down, where yaw is
	// meaningless.
	const float Pitch = FRotator::NormalizeAxis(Centre.Pitch);
	const float PitchRoom = FMath::Clamp(PitchLeeway, 0.0f, 89.0f);
	const float YawRoom = FMath::Clamp(YawLeeway, 0.0f, 179.0f);

	PlayerCameraManager->ViewPitchMin = FMath::Clamp(Pitch - PitchRoom, -89.0f, 89.0f);
	PlayerCameraManager->ViewPitchMax = FMath::Clamp(Pitch + PitchRoom, -89.0f, 89.0f);
	PlayerCameraManager->ViewYawMin = FRotator::ClampAxis(Centre.Yaw - YawRoom);
	PlayerCameraManager->ViewYawMax = FRotator::ClampAxis(Centre.Yaw + YawRoom);
}

void ADinoPlayerController::ReleaseViewConstraint()
{
	if (!bViewConstrained)
	{
		return;
	}
	bViewConstrained = false;

	if (PlayerCameraManager)
	{
		PlayerCameraManager->ViewPitchMin = SavedViewPitchMin;
		PlayerCameraManager->ViewPitchMax = SavedViewPitchMax;
		PlayerCameraManager->ViewYawMin = SavedViewYawMin;
		PlayerCameraManager->ViewYawMax = SavedViewYawMax;
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

	// A restart mid-pin destroys the body without its pin ever ending; a fresh character starts
	// with a free view.
	ReleaseViewConstraint();

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

void ADinoPlayerController::DinoVisionTest(const FString& Mode)
{
	if (!HasAuthority())
	{
		DinoScreenError(TEXT("DinoVisionTest only works on the host - AI runs there."));
		return;
	}

	TArray<ADinoAIControllerBase*> Creatures;
	bool bAllInTest = true;
	for (TActorIterator<ADinoAIControllerBase> It(GetWorld()); It; ++It)
	{
		Creatures.Add(*It);
		bAllInTest &= It->IsInVisionTest();
	}

	if (Creatures.IsEmpty())
	{
		DinoScreenError(TEXT("No AI creatures in the level."));
		return;
	}

	// Toggle switches everything on unless everything is already on - so with a mix of placed
	// test creatures and normal ones, the first toggle brings them all into line.
	bool bEnable = !bAllInTest;
	if (Mode.Equals(TEXT("on"), ESearchCase::IgnoreCase) || Mode == TEXT("1"))
	{
		bEnable = true;
	}
	else if (Mode.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Mode == TEXT("0"))
	{
		bEnable = false;
	}
	else if (!Mode.Equals(TEXT("toggle"), ESearchCase::IgnoreCase))
	{
		DinoScreenError(FString::Printf(TEXT("Unknown mode \"%s\". Use on, off, or nothing to toggle."), *Mode));
		return;
	}

	for (ADinoAIControllerBase* Creature : Creatures)
	{
		Creature->SetVisionTest(bEnable);
	}

	DinoScreenLog(bEnable
		? FString::Printf(TEXT("Vision test ON for %d creature(s): frozen, no memory. Green line = sees you, red = can't (with the reason)."), Creatures.Num())
		: FString::Printf(TEXT("Vision test OFF for %d creature(s): normal AI resumed."), Creatures.Num()),
		FColor(255, 220, 60), 8.0f);
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

void ADinoPlayerController::DinoMicRefresh()
{
	if (UDinoVoiceSubsystem* VoiceSettings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDinoVoiceSubsystem>() : nullptr)
	{
		VoiceSettings->RefreshMic();
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

	// The mic over the last few seconds rather than this instant: typing this command means not
	// talking, so an instant reading always said "no". Speak, then run it.
	const UDinoVoiceSubsystem* VoiceSettings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;
	if (VoiceSettings)
	{
		TArray<FString> MicLines;
		VoiceSettings->DescribeMic(MicLines);
		for (const FString& Line : MicLines)
		{
			DinoScreenLog(Line, FColor::White, StatusDuration);
		}
	}

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
			const ADinoPlayerState* DinoOther = Cast<ADinoPlayerState>(Other);
			const UDinoVoiceTalker* Talker = DinoOther ? DinoOther->GetVoiceTalker() : nullptr;
			const bool bMutedByMe = VoiceSettings && VoiceSettings->IsPlayerMuted(OtherId);
			// "NOT REGISTERED" means the engine plays their voice with none of our settings: at
			// full volume, from anywhere, whatever "from their body" says.
			DinoScreenLog(FString::Printf(TEXT("    %-20s %s, %s%s%s"), *Other->GetPlayerName(),
				!OtherId.IsValid() ? TEXT("<no net id>") : (bTalking ? TEXT("talking") : TEXT("silent")),
				Talker && Talker->IsPositioned() ? TEXT("from their body") : TEXT("unpositioned"),
				Talker && Talker->IsEngineTalker() ? TEXT("") : TEXT(", NOT REGISTERED"),
				bMutedByMe ? TEXT(", muted by you") : TEXT("")), FColor::White, StatusDuration);
		}
	}

	// The engine's own dump stays log-only: it is dozens of lines and would bury everything
	// above it on screen. Read it from Saved/Logs when the summary above is not enough.
	UE_LOG(LogDinoNet, Log, TEXT("  engine dump:\n%s"), *Voice->GetVoiceDebugState());
}
