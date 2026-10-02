#include "DinoGameMode.h"

#include "DinoGame.h"
#include "DinoGameState.h"
#include "DinoPlayerController.h"
#include "DinoPlayerState.h"
#include "DinoSpectatorPawn.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Online/DinoSessionSubsystem.h"
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
		DinoState->SetIsAlive(false);
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
			DinoState->SetIsAlive(true);
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
	Player->ChangeState(NAME_Spectating);
	Player->ClientGotoState(NAME_Spectating);
}

void ADinoGameMode::NotifyPlayerDied(AController* Victim)
{
	LastDeathTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

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

	// Seamless travel tracks players still loading in NumTravellingPlayers. A full reconnect -
	// which is what PIE always does - has no such count, so also wait for the head count the
	// restart recorded, with a timeout so one player who never comes back cannot block it.
	const bool bEveryoneHere = NumTravellingPlayers == 0 && GetNumPlayers() >= AutoStartPlayerCount;
	const bool bTimedOut = GetWorld() && GetWorld()->GetTimeSeconds() >= AutoStartTimeout;

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
