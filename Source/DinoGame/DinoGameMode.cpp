#include "DinoGameMode.h"

#include "DinoCharacter.h"
#include "DinoDamageTypes.h"
#include "DinoGame.h"
#include "DinoGameState.h"
#include "DinoPlayerController.h"
#include "DinoDowned.h"
#include "DinoHealthRules.h"
#include "DinoPlayerState.h"
#include "DinoSpectatorPawn.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Online/DinoSessionSubsystem.h"
#include "Online/DinoVoiceRules.h"
#include "Online/DinoVoiceSubsystem.h"
#include "TimerManager.h"

namespace
{
	/**
	 * URL option carried across a restart. Its value is the number of players to wait for; 0
	 * means "go to the lobby".
	 *
	 * Always written explicitly, including as 0, because restarts are relative travels and a
	 * relative URL inherits the previous one's options. Leave it off a return-to-lobby and the
	 * option from the last restart carries over, so the "lobby" starts a round by itself.
	 */
	const TCHAR* AutoStartOption = TEXT("DinoAutoStart");
}

ADinoGameMode::ADinoGameMode()
{
	GameStateClass = ADinoGameState::StaticClass();
	PlayerStateClass = ADinoPlayerState::StaticClass();
	PlayerControllerClass = ADinoPlayerController::StaticClass();
	SpectatorClass = ADinoSpectatorPawn::StaticClass();

	// Keeps PlayerStates and their replicated data alive across level changes, so a co-op run
	// can move between maps without every player being torn down and rebuilt.
	bUseSeamlessTravel = true;

	// Every match waits in the lobby until the host starts it. Without this AGameMode starts
	// the moment one player is present.
	bDelayedStart = true;
}

void ADinoGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	AutoStartPlayerCount = UGameplayStatics::GetIntOption(Options, AutoStartOption, 0);
	if (AutoStartPlayerCount > 0)
	{
		UE_LOG(LogDinoNet, Log, TEXT("Round will start once %d player(s) have arrived."), AutoStartPlayerCount);
	}
}

void ADinoGameMode::InitGameState()
{
	Super::InitGameState();

	// The session subsystem lives on the game instance and survives travel, so the code is still
	// there after every map load. Read here, at the start of each map, rather than pushed from
	// the moment of hosting - a restart builds a fresh game state that would otherwise be blank.
	ADinoGameState* DinoState = GetGameState<ADinoGameState>();
	const UGameInstance* GameInstance = GetGameInstance();
	const UDinoSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UDinoSessionSubsystem>() : nullptr;
	if (DinoState && Sessions)
	{
		DinoState->SetJoinCode(Sessions->GetCurrentJoinCode());
	}

	// The game state is new on every map, so these are bound again on every map.
	if (DinoState)
	{
		DinoState->OnPlayerStatusChanged.AddUniqueDynamic(this, &ADinoGameMode::HandlePlayerStatusChanged);
		DinoState->OnPlayerListChanged.AddUniqueDynamic(this, &ADinoGameMode::HandlePlayerListChanged);
	}
}

// --- Voice rules -----------------------------------------------------------------------------

void ADinoGameMode::OnMatchStateSet()
{
	Super::OnMatchStateSet();
	RefreshVoiceRules();
}

void ADinoGameMode::HandlePlayerStatusChanged(ADinoPlayerState* /*ChangedPlayer*/, EDinoPlayerStatus /*OldStatus*/, EDinoPlayerStatus /*NewStatus*/)
{
	RefreshVoiceRules();
}

void ADinoGameMode::HandlePlayerListChanged()
{
	RefreshVoiceRules();
}

void ADinoGameMode::RefreshVoiceRules()
{
	UWorld* World = GetWorld();
	if (!World || !GameState)
	{
		return;
	}

	// The rule itself is DinoVoiceRules::ShouldMute, shared with the self-check and the tests.
	const bool bRoundRunning = IsMatchInProgress();
	const UGameInstance* GameInstance = GetGameInstance();
	const UDinoVoiceSubsystem* VoiceSettings = GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;

	FString Muted;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Listener = It->Get();
		const ADinoPlayerState* ListenerState = Listener ? Listener->GetPlayerState<ADinoPlayerState>() : nullptr;
		if (!ListenerState)
		{
			continue;
		}

		const ADinoPlayerController* DinoListener = Cast<ADinoPlayerController>(Listener);
		const bool bListenerAlive = ListenerState->IsAlive();
		const bool bSpectatorHearsLiving = !DinoListener || DinoListener->HearsLivingWhileSpectating();

		for (APlayerState* Other : GameState->PlayerArray)
		{
			const ADinoPlayerState* Speaker = Cast<ADinoPlayerState>(Other);
			if (!Speaker || Speaker == ListenerState || !Speaker->GetUniqueId().IsValid())
			{
				continue;
			}

			const bool bMute = DinoVoiceRules::ShouldMute(bRoundRunning, bListenerAlive, Speaker->IsAlive(), bSpectatorHearsLiving);

			if (bMute)
			{
				Listener->GameplayMutePlayer(Speaker->GetUniqueId());
				Muted += FString::Printf(TEXT(" %s-x->%s"), *Speaker->GetPlayerName(), *ListenerState->GetPlayerName());
			}
			else
			{
				// Only the host's own Tab-menu mutes are visible here; see UnmuteForGameplay.
				const bool bHostMutedThem = Listener->IsLocalController() && VoiceSettings && VoiceSettings->IsPlayerMuted(Speaker->GetUniqueId());
				UnmuteForGameplay(Listener, Speaker->GetUniqueId(), Listener->IsLocalController(), bHostMutedThem);
			}
		}
	}

	if (Muted != LastVoiceRules)
	{
		LastVoiceRules = Muted;
		UE_LOG(LogDinoNet, Log, TEXT("Voice rules: %s"),
			Muted.IsEmpty() ? TEXT("everyone hears everyone") : *FString::Printf(TEXT("muted (speaker -x-> listener):%s"), *Muted));
	}
}

void ADinoGameMode::UnmuteForGameplay(APlayerController* Listener, const FUniqueNetIdRepl& SpeakerId, bool bListenerIsHost, bool bListenerMutedThem)
{
	const FUniqueNetIdPtr Id = SpeakerId.GetUniqueNetId();
	const EVoiceBlockReasons* Reasons = Id.IsValid() ? Listener->MuteList.VoicePacketFilterMap.Find(Id.ToSharedRef()) : nullptr;
	const bool bWasGameplayMuted = Reasons && EnumHasAnyFlags(*Reasons, EVoiceBlockReasons::Gameplay);

	Listener->GameplayUnmutePlayer(SpeakerId);

	// The host's own controller is both the server's and the client's copy, and they share one
	// mute list. The gameplay mute's ClientMutePlayer then adds a second, "Muted" reason to that
	// same list, so the gameplay unmute above still sees a reason left and never unmutes: the
	// host would stop hearing that player for the rest of the session. Remote clients keep the
	// two halves on different machines and are unaffected. Skipped when the host muted that
	// player themselves in the Tab menu, which is the same "Muted" reason, meant to stay.
	if (bWasGameplayMuted && bListenerIsHost && !bListenerMutedThem)
	{
		UE_LOG(LogDinoNet, Log, TEXT("Voice: host unmute workaround applied for %s."), *SpeakerId.ToString());
		Listener->ClientUnmutePlayer(SpeakerId);
	}
}

void ADinoGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UE_LOG(LogDinoNet, Log, TEXT("Player joined. Total players: %d"), GetNumPlayers());
	OnPlayerCountChanged(GetNumPlayers());
}

void ADinoGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	UE_LOG(LogDinoNet, Log, TEXT("Player left. Total players: %d"), GetNumPlayers());
	OnPlayerCountChanged(GetNumPlayers());

	// No explicit round-over check needed: ReadyToEndMatch is polled every tick and counts the
	// living, so the last survivor disconnecting ends the round the same way dying does.
}

// --- Players ---------------------------------------------------------------------------------

void ADinoGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Here rather than in PostLogin: after a seamless restart the engine skips PostLogin and calls
	// HandleSeamlessTravelPlayer, which also ends up here. Setting these in PostLogin lost the
	// host marker on every restart, and could carry a player's alive flag over from the last round.
	if (ADinoPlayerState* DinoState = NewPlayer ? NewPlayer->GetPlayerState<ADinoPlayerState>() : nullptr)
	{
		// On a listen server the host's controller is the only one that is local to the server.
		DinoState->SetIsHost(NewPlayer->IsLocalController());
		DinoState->SetStatus(EDinoPlayerStatus::Lobby);
	}

	// Spawns nobody here, thanks to PlayerCanRestart; may start the match if ReadyToStartMatch,
	// in which case RestartPlayer marks this player alive again.
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	// Lobby players and late joiners watch until a round hands them a character. Done
	// explicitly: after a fresh join the engine leaves a pawnless player in no particular
	// state, with a camera stuck wherever the controller happened to spawn.
	if (NewPlayer && !NewPlayer->GetPawn())
	{
		BeginSpectating(NewPlayer);
	}
}

bool ADinoGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	return bStartingRound && Super::PlayerCanRestart_Implementation(Player);
}

void ADinoGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);

	if (NewPlayer && NewPlayer->GetPawn())
	{
		if (ADinoPlayerState* DinoState = NewPlayer->GetPlayerState<ADinoPlayerState>())
		{
			DinoState->SetStatus(EDinoPlayerStatus::Alive);
		}
	}
}

void ADinoGameMode::BeginSpectating(APlayerController* Player)
{
	if (!Player)
	{
		return;
	}

	// The engine's spectating state unpossesses the body on the server and spawns SpectatorClass
	// on the owning client only. Both halves are needed: ChangeState alone leaves the client
	// believing it is still playing, with no camera.
	//
	// The dead body is deliberately left in the world - hidden, with no collision, since death -
	// until the map reloads. Deleting it here froze joining players' spectator cameras: on their
	// machine the deletion could arrive while the controller still held the body, and Unreal's
	// answer to "my pawn was destroyed" (AController::PawnPendingDestroy) is to switch the
	// controller to Inactive, which destroys the spectator camera just created. It also keeps the
	// way open for a visible death marker.
	Player->ChangeState(NAME_Spectating);
	Player->ClientGotoState(NAME_Spectating);
}

void ADinoGameMode::DownPlayer(AController* Victim, TSubclassOf<UDamageType> Cause)
{
	ADinoPlayerState* VictimState = Victim ? Victim->GetPlayerState<ADinoPlayerState>() : nullptr;
	if (!VictimState || VictimState->GetStatus() != EDinoPlayerStatus::Alive)
	{
		return;
	}

	VictimState->SetStatus(EDinoPlayerStatus::Downed);
	UE_LOG(LogDinoNet, Log, TEXT("%s is down (%s)."), *VictimState->GetPlayerName(), *UDinoDamageType::NameOf(Cause));

	BleedOutEveryoneIfNoOneStanding();
}

void ADinoGameMode::RevivePlayer(AController* Reviver, AController* Revived)
{
	ADinoPlayerState* RevivedState = Revived ? Revived->GetPlayerState<ADinoPlayerState>() : nullptr;
	ADinoCharacter* Body = Revived ? Cast<ADinoCharacter>(Revived->GetPawn()) : nullptr;
	if (!RevivedState || RevivedState->GetStatus() != EDinoPlayerStatus::Downed || !Body)
	{
		return;
	}

	// Body first: its own transition logs and restores movement, then the status says so.
	Body->Revive(DinoDowned::ReviveHealthFraction());
	RevivedState->SetStatus(EDinoPlayerStatus::Alive);

	const APlayerState* ReviverState = Reviver ? Reviver->PlayerState : nullptr;
	UE_LOG(LogDinoNet, Log, TEXT("%s was revived by %s."), *RevivedState->GetPlayerName(),
		ReviverState ? *ReviverState->GetPlayerName() : TEXT("?"));
}

void ADinoGameMode::BleedOutEveryoneIfNoOneStanding()
{
	if (!IsMatchInProgress() || !GameState)
	{
		return;
	}

	int32 Standing = 0;
	TArray<AController*> Downed;
	for (APlayerState* State : GameState->PlayerArray)
	{
		const ADinoPlayerState* Player = Cast<ADinoPlayerState>(State);
		if (!Player)
		{
			continue;
		}
		if (Player->GetStatus() == EDinoPlayerStatus::Alive)
		{
			++Standing;
		}
		else if (Player->GetStatus() == EDinoPlayerStatus::Downed && Player->GetOwningController())
		{
			Downed.Add(Player->GetOwningController());
		}
	}

	if (!DinoHealthRules::ShouldBleedOutEveryone(Standing, Downed.Num()))
	{
		return;
	}

	UE_LOG(LogDinoNet, Log, TEXT("Nobody is left standing to revive %d downed player(s); they bleed out."), Downed.Num());
	for (AController* Player : Downed)
	{
		KillPlayer(Player, UDinoDamageType_BleedOut::StaticClass());
	}
}

void ADinoGameMode::KillPlayer(AController* Victim, TSubclassOf<UDamageType> Cause)
{
	ADinoPlayerState* VictimState = Victim ? Victim->GetPlayerState<ADinoPlayerState>() : nullptr;

	// Once only. The body's own death calls back in here (step 2 below, and lethal damage), and a
	// player in the lobby or already dead has nothing left to lose.
	if (!VictimState || !VictimState->IsAlive())
	{
		return;
	}

	// 1. Status first, so the round-end check and every listener already see them as dead.
	VictimState->SetStatus(EDinoPlayerStatus::Dead, Cause);

	// 2. The body. A no-op when lethal damage got here first; the only route for a fall.
	if (ADinoCharacter* Body = Cast<ADinoCharacter>(Victim->GetPawn()))
	{
		Body->Die(Cause);
	}

	// 3. Round over waits SpectateDelay from this moment (ReadyToEndMatch); spectating starts
	// after the same delay, on the timer at the end.
	LastDeathTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// 4. One line per death, on the host, with what did it.
	UE_LOG(LogDinoNet, Log, TEXT("%s %s (%s)."), *VictimState->GetPlayerName(),
		*UDinoDamageType::DescribeDeath(Cause), *UDinoDamageType::NameOf(Cause));

	// The last one standing dying leaves anyone still down with nobody to revive them.
	BleedOutEveryoneIfNoOneStanding();

	APlayerController* Player = Cast<APlayerController>(Victim);
	if (!Player)
	{
		return;
	}

	const TWeakObjectPtr<APlayerController> WeakPlayer(Player);
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, WeakPlayer]()
	{
		// Skipped if the map is already reloading - a restart can land inside the delay.
		APlayerController* Dead = WeakPlayer.Get();
		if (Dead && (IsMatchInProgress() || GetMatchState() == MatchState::WaitingPostMatch))
		{
			BeginSpectating(Dead);
		}
	}), FMath::Max(SpectateDelay, 0.01f), false);
}

// --- Match flow ------------------------------------------------------------------------------

bool ADinoGameMode::ReadyToStartMatch_Implementation()
{
	// Manual start otherwise - bDelayedStart is what makes Super return false.
	if (AutoStartPlayerCount <= 0 || GetMatchState() != MatchState::WaitingToStart || GetNumPlayers() == 0)
	{
		return false;
	}

	// Never before the world has begun play. When everyone is already present as the map starts
	// - always, in solo - AGameMode asks this from StartPlay, and a "yes" there makes it skip
	// NotifyBeginPlay in HandleMatchIsWaitingToStart and start the round first: characters are
	// spawned and possessed before the world they are in has begun play, and the player could
	// neither move nor look. Waiting one tick puts a restart through the same order as the
	// lobby's Start button - world begins play, then the round starts - which is the path known
	// to work. AGameMode::Tick asks again every frame, so the delay is a single tick.
	if (!GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return false;
	}

	// Seamless travel tracks players still loading in NumTravellingPlayers. A full reconnect -
	// which is what PIE always does - has no such count, so also wait for the head count the
	// restart recorded, with a timeout so one player who never comes back cannot block it.
	//
	// NumTravellingPlayers matters twice over: AGameMode::GetNumPlayers() counts players still
	// loading as well as those who have arrived, so the head count alone would be met before
	// anyone but the host had loaded - and a player who has not loaded cannot be spawned.
	const bool bEveryoneHere = NumTravellingPlayers == 0 && GetNumPlayers() >= AutoStartPlayerCount;

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (AutoStartDeadline < 0.0)
	{
		AutoStartDeadline = Now + AutoStartTimeout;
	}
	const bool bTimedOut = Now >= AutoStartDeadline;

	if (bTimedOut && !bEveryoneHere)
	{
		UE_LOG(LogDinoNet, Warning, TEXT("Auto-start timed out after %.0fs with %d travelling - starting without them."),
			AutoStartTimeout, NumTravellingPlayers);
	}

	return bEveryoneHere || bTimedOut;
}

bool ADinoGameMode::ReadyToEndMatch_Implementation()
{
	if (!IsMatchInProgress())
	{
		return false;
	}

	const ADinoGameState* DinoState = GetGameState<ADinoGameState>();
	if (!DinoState || DinoState->GetAlivePlayerCount() > 0)
	{
		return false;
	}

	// Hold round over for the same beat as spectating, so the last player to die sees it happen
	// before the menu covers it.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return LastDeathTime < 0.0 || Now - LastDeathTime >= SpectateDelay;
}

void ADinoGameMode::HandleMatchHasStarted()
{
	UE_LOG(LogDinoNet, Log, TEXT("Round starting with %d player(s)."), GetNumPlayers());

	// Spawning happens inside Super - see bStartingRound.
	bStartingRound = true;
	Super::HandleMatchHasStarted();
	bStartingRound = false;

	// One-shot: the next lobby on this map should wait for the host again.
	AutoStartPlayerCount = 0;
	LastDeathTime = -1.0;
}

bool ADinoGameMode::StartRound()
{
	if (GetMatchState() != MatchState::WaitingToStart)
	{
		return false;
	}

	StartMatch();
	return true;
}

void ADinoGameMode::RestartRound()
{
	ReloadMap(true);
}

void ADinoGameMode::ReturnToLobby()
{
	ReloadMap(false);
}

void ADinoGameMode::ReloadMap(bool bAutoStartRound)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// The map's package path, without the PIE prefix the editor adds to it.
	const FString MapPath = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	const FString URL = FString::Printf(TEXT("%s?%s=%d"), *MapPath, AutoStartOption,
		bAutoStartRound ? GetNumPlayers() : 0);

	UE_LOG(LogDinoNet, Log, TEXT("%s: travelling to %s"),
		bAutoStartRound ? TEXT("Restarting round") : TEXT("Returning to lobby"), *URL);

	// Relative, not absolute: a relative travel keeps the current URL's options, and one of
	// them is ?listen. Dropping it would leave the host standalone and every client stranded.
	World->ServerTravel(URL, /*bAbsolute*/ false);
}
