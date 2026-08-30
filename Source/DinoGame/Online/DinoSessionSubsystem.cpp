#include "Online/DinoSessionSubsystem.h"

#include "DinoBuildInfo.h"
#include "DinoGame.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"

namespace
{
	// No 0/O and no 1/I/L. These codes are read aloud over voice chat and retyped from memory,
	// and those are exactly the characters people get wrong. 31 symbols at 6 characters is
	// still ~887 million combinations.
	const TCHAR* JoinCodeAlphabet = TEXT("23456789ABCDEFGHJKMNPQRSTUVWXYZ");
	const int32 JoinCodeLength = 6;

	/** Session setting key the code is advertised under, and filtered on when joining. */
	const FName SETTING_DINOJOINCODE(TEXT("DINOJOINCODE"));
}

IOnlineSessionPtr UDinoSessionSubsystem::GetSessions() const
{
	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	return Subsystem ? Subsystem->GetSessionInterface() : nullptr;
}

bool UDinoSessionSubsystem::IsLANMode() const
{
	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	return Subsystem == nullptr || Subsystem->GetSubsystemName() == NULL_SUBSYSTEM;
}

bool UDinoSessionSubsystem::IsInSession() const
{
	const IOnlineSessionPtr Sessions = GetSessions();
	return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr;
}

void UDinoSessionSubsystem::HostSession(const FString& MapName, int32 MaxPlayers, bool bPrivate)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		UE_LOG(LogDinoNet, Error, TEXT("HostSession: no session interface available."));
		OnHostComplete.Broadcast(false);
		return;
	}

	// An empty map name means "whatever is already loaded". Resolving it here rather than in
	// each caller means the menu and the console command cannot drift apart.
	FString ResolvedMap = MapName;
	if (ResolvedMap.IsEmpty())
	{
		const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		ResolvedMap = World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString();
	}

	if (ResolvedMap.IsEmpty())
	{
		DinoScreenError(TEXT("Could not work out which map to host."));
		OnHostComplete.Broadcast(false);
		return;
	}

	PendingMapName = ResolvedMap;
	PendingMaxPlayers = FMath::Max(1, MaxPlayers);
	bPendingPrivate = bPrivate;

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		PendingAction = EPendingAction::Rehost;
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleDestroyComplete));

		if (!Sessions->DestroySession(NAME_GameSession))
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
			PendingAction = EPendingAction::None;
			OnHostComplete.Broadcast(false);
		}
		return;
	}

	CreateSessionNow();
}

void UDinoSessionSubsystem::CreateSessionNow()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		OnHostComplete.Broadcast(false);
		return;
	}

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = bPendingPrivate ? 0 : PendingMaxPlayers;
	Settings.NumPrivateConnections = bPendingPrivate ? PendingMaxPlayers : 0;
	Settings.bShouldAdvertise = !bPendingPrivate;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bIsDedicated = false;
	Settings.bIsLANMatch = IsLANMode();
	// Steam routes lobby traffic through its relay network, which is what lets a host with no
	// port forwarding and no public IP accept connections.
	Settings.bUseLobbiesIfAvailable = true;
	Settings.Set(SETTING_MAPNAME, PendingMapName, EOnlineDataAdvertisementType::ViaOnlineService);

	// Advertised, not just stored: ViaOnlineService is what puts it in Steam lobby metadata,
	// which is what lets a joiner filter on it without downloading the lobby list first.
	CurrentJoinCode = GenerateJoinCode();
	Settings.Set(SETTING_DINOJOINCODE, CurrentJoinCode, EOnlineDataAdvertisementType::ViaOnlineService);

	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleCreateComplete));

	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		UE_LOG(LogDinoNet, Error, TEXT("CreateSession call was rejected."));
		OnHostComplete.Broadcast(false);
	}
}

void UDinoSessionSubsystem::HandleCreateComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	}

	if (!bWasSuccessful)
	{
		DinoScreenError(FString::Printf(TEXT("Failed to create session %s."), *SessionName.ToString()));
		OnHostComplete.Broadcast(false);
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		OnHostComplete.Broadcast(false);
		return;
	}

	DinoScreenLog(FString::Printf(TEXT("Session created. Travelling to %s as listen server."), *PendingMapName), FColor::Green);
	// Five minutes, because the host has to read this out to someone who may not be at their
	// desk yet. GetCurrentJoinCode() is the version a menu should show.
	DinoScreenLog(FString::Printf(TEXT("JOIN CODE: %s"), *CurrentJoinCode), FColor::Yellow, 300.0f);

	// Seamless travel cannot promote a Standalone game into a listen server: it deliberately
	// reuses the existing network context, and in doing so drops the "?listen" option, leaving
	// the game standalone with no net driver. The initial host transition therefore has to be a
	// hard travel. Clearing the flag on the outgoing GameMode is enough — a hard travel destroys
	// it, and ADinoGameMode's constructor sets bUseSeamlessTravel back to true on the new
	// instance, so later in-session map changes stay seamless.
	if (AGameModeBase* GameMode = World->GetAuthGameMode())
	{
		GameMode->bUseSeamlessTravel = false;
	}

	OnHostComplete.Broadcast(true);
	World->ServerTravel(FString::Printf(TEXT("%s?listen"), *PendingMapName));
}

void UDinoSessionSubsystem::FindSessions(int32 MaxResults)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		OnFindComplete.Broadcast(false, TArray<FDinoSessionInfo>());
		return;
	}

	Search = MakeShared<FOnlineSessionSearch>();
	Search->MaxSearchResults = FMath::Max(1, MaxResults);
	Search->bIsLanQuery = IsLANMode();
	Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(SEARCH_DEDICATED_ONLY, false, EOnlineComparisonOp::Equals);

	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleFindComplete));

	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Search.Reset();
		OnFindComplete.Broadcast(false, TArray<FDinoSessionInfo>());
	}
}

void UDinoSessionSubsystem::HandleFindComplete(bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}

	TArray<FDinoSessionInfo> Results;
	if (bWasSuccessful && Search.IsValid())
	{
		for (int32 i = 0; i < Search->SearchResults.Num(); ++i)
		{
			const FOnlineSessionSearchResult& Result = Search->SearchResults[i];
			const FOnlineSessionSettings& Settings = Result.Session.SessionSettings;

			FDinoSessionInfo Info;
			Info.Index = i;
			Info.HostName = Result.Session.OwningUserName;
			Info.PingMs = Result.PingInMs;
			Info.MaxPlayers = Settings.NumPublicConnections + Settings.NumPrivateConnections;
			Info.CurrentPlayers = Info.MaxPlayers - (Result.Session.NumOpenPublicConnections + Result.Session.NumOpenPrivateConnections);
			Settings.Get(SETTING_MAPNAME, Info.MapName);

			Results.Add(Info);
		}
	}

	// A JoinByCode search reports through OnJoinComplete rather than as a list: the player asked
	// to join one specific session, not to browse. Consume the pending code first so an early
	// return cannot leave it set for the next unrelated search.
	if (!PendingJoinCode.IsEmpty())
	{
		const FString Code = PendingJoinCode;
		PendingJoinCode.Reset();

		if (!bWasSuccessful)
		{
			DinoScreenError(FString::Printf(TEXT("Search failed while looking for code %s."), *Code));
			OnJoinComplete.Broadcast(false);
			return;
		}

		if (Results.Num() == 0)
		{
			DinoScreenError(FString::Printf(TEXT("No session found with code %s. Check the code, and that the host is still hosting."), *Code));
			OnJoinComplete.Broadcast(false);
			return;
		}

		DinoScreenLog(FString::Printf(TEXT("Found code %s (host %s). Joining..."), *Code, *Results[0].HostName), FColor::Green);
		JoinFoundSession(0);
		return;
	}

	if (bWasSuccessful)
	{
		// Long duration: this is a list the player has to read an index out of and then type.
		DinoScreenLog(FString::Printf(TEXT("Search complete: %d session(s)."), Results.Num()), FColor::Green, 45.0f);
		for (const FDinoSessionInfo& Info : Results)
		{
			DinoScreenLog(
				FString::Printf(TEXT("  [%d] host=%s map=%s players=%d/%d ping=%dms"),
					Info.Index, *Info.HostName, *Info.MapName, Info.CurrentPlayers, Info.MaxPlayers, Info.PingMs),
				FColor::White, 45.0f);
		}
	}
	else
	{
		DinoScreenError(TEXT("Session search failed."));
	}

	OnFindComplete.Broadcast(bWasSuccessful, Results);
}

void UDinoSessionSubsystem::JoinFoundSession(int32 SessionIndex)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid() || !Search.IsValid() || !Search->SearchResults.IsValidIndex(SessionIndex))
	{
		OnJoinComplete.Broadcast(false);
		return;
	}

	// Anything left registered under NAME_GameSession — a session this player hosted, or one a
	// half-finished join left behind — makes JoinSession fail with AlreadyInSession. Clear it
	// first, using the same destroy-then-retry cycle the host path uses.
	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		UE_LOG(LogDinoNet, Log, TEXT("Leaving the current session before joining."));

		PendingAction = EPendingAction::Join;
		PendingJoinIndex = SessionIndex;

		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleDestroyComplete));

		if (!Sessions->DestroySession(NAME_GameSession))
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
			PendingAction = EPendingAction::None;
			PendingJoinIndex = INDEX_NONE;
			OnJoinComplete.Broadcast(false);
		}
		return;
	}

	JoinSessionNow(SessionIndex);
}

void UDinoSessionSubsystem::JoinSessionNow(int32 SessionIndex)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid() || !Search.IsValid() || !Search->SearchResults.IsValidIndex(SessionIndex))
	{
		OnJoinComplete.Broadcast(false);
		return;
	}

	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleJoinComplete));

	if (!Sessions->JoinSession(0, NAME_GameSession, Search->SearchResults[SessionIndex]))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		OnJoinComplete.Broadcast(false);
	}
}

void UDinoSessionSubsystem::HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}

	if (!Sessions.IsValid() || Result != EOnJoinSessionCompleteResult::Success)
	{
		DinoScreenError(FString::Printf(TEXT("Join failed for %s (result %d)."), *SessionName.ToString(), static_cast<int32>(Result)));
		OnJoinComplete.Broadcast(false);
		return;
	}

	FString ConnectString;
	APlayerController* PlayerController = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!PlayerController || !Sessions->GetResolvedConnectString(SessionName, ConnectString))
	{
		// The join itself succeeded, so the session is registered even though there is no way to
		// travel into it. Leaving it registered would block every later join with
		// AlreadyInSession, so tear it down rather than stranding the player.
		DinoScreenError(FString::Printf(
			TEXT("Joined %s but could not resolve a connect string. Leaving it so later joins are not blocked."),
			*SessionName.ToString()));

		Sessions->DestroySession(NAME_GameSession);
		OnJoinComplete.Broadcast(false);
		return;
	}

	DinoScreenLog(FString::Printf(TEXT("Joined %s. Travelling to %s."), *SessionName.ToString(), *ConnectString), FColor::Green);

	OnJoinComplete.Broadcast(true);
	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
}

void UDinoSessionSubsystem::LeaveSession()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		OnLeaveComplete.Broadcast(false);
		return;
	}

	PendingAction = EPendingAction::None;
	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleDestroyComplete));

	if (!Sessions->DestroySession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		OnLeaveComplete.Broadcast(false);
	}
}

void UDinoSessionSubsystem::HandleDestroyComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}

	// Consume the pending action before acting on it, so a failure inside the retry cannot leave
	// the subsystem primed to retry again on the next destroy.
	const EPendingAction Action = PendingAction;
	const int32 JoinIndex = PendingJoinIndex;
	PendingAction = EPendingAction::None;
	PendingJoinIndex = INDEX_NONE;

	if (Action == EPendingAction::Rehost)
	{
		if (bWasSuccessful)
		{
			CreateSessionNow();
		}
		else
		{
			DinoScreenError(TEXT("Could not clear the previous session, so hosting was abandoned."));
			OnHostComplete.Broadcast(false);
		}
		return;
	}

	if (Action == EPendingAction::Join)
	{
		if (bWasSuccessful)
		{
			JoinSessionNow(JoinIndex);
		}
		else
		{
			DinoScreenError(TEXT("Could not leave the previous session, so joining was abandoned."));
			OnJoinComplete.Broadcast(false);
		}
		return;
	}

	// Only on a real leave - the rehost and join paths returned above, and both want the code
	// they are about to replace left alone until then.
	CurrentJoinCode.Reset();

	DinoScreenLog(FString::Printf(TEXT("Left session %s (%s)."), *SessionName.ToString(),
		bWasSuccessful ? TEXT("ok") : TEXT("failed")));

	OnLeaveComplete.Broadcast(bWasSuccessful);
}

// --- Join codes ------------------------------------------------------------------------------

FString UDinoSessionSubsystem::GenerateJoinCode()
{
	const int32 AlphabetSize = FCString::Strlen(JoinCodeAlphabet);

	FString Code;
	Code.Reserve(JoinCodeLength);
	for (int32 i = 0; i < JoinCodeLength; ++i)
	{
		Code.AppendChar(JoinCodeAlphabet[FMath::RandRange(0, AlphabetSize - 1)]);
	}

	return Code;
}

FString UDinoSessionSubsystem::NormaliseJoinCode(const FString& Raw)
{
	// Drop anything outside the alphabet rather than rejecting it. People type "abc def",
	// "ABC-DEF", or paste it with a trailing space, and all three mean the same code.
	FString Out;
	Out.Reserve(Raw.Len());

	for (const TCHAR Ch : Raw)
	{
		const TCHAR Upper = FChar::ToUpper(Ch);
		if (FCString::Strchr(JoinCodeAlphabet, Upper) != nullptr)
		{
			Out.AppendChar(Upper);
		}
	}

	return Out;
}

void UDinoSessionSubsystem::JoinByCode(const FString& JoinCode)
{
	const FString Code = NormaliseJoinCode(JoinCode);

	if (Code.Len() != JoinCodeLength)
	{
		DinoScreenError(FString::Printf(TEXT("Join code must be %d characters. Got \"%s\"."), JoinCodeLength, *Code));
		OnJoinComplete.Broadcast(false);
		return;
	}

	DinoScreenLog(FString::Printf(TEXT("Looking for code %s..."), *Code));

	PendingJoinCode = Code;
	FindSessionByCode(Code);
}

void UDinoSessionSubsystem::FindSessionByCode(const FString& JoinCode)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		PendingJoinCode.Reset();
		DinoScreenError(TEXT("No session interface available - is Steam running?"));
		OnJoinComplete.Broadcast(false);
		return;
	}

	Search = MakeShared<FOnlineSessionSearch>();
	// A code matches one session, so a large result cap buys nothing. Keeping it small also
	// keeps the failure honest: if the filter ever stops working, this returns junk rather
	// than silently joining a stranger's lobby out of fifty.
	Search->MaxSearchResults = 10;
	Search->bIsLanQuery = IsLANMode();
	Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(SEARCH_DEDICATED_ONLY, false, EOnlineComparisonOp::Equals);

	// The line that makes this a join code rather than a browse. Steam applies this filter
	// lobby-side, so AppID 480's shared Spacewar traffic never reaches us at all.
	Search->QuerySettings.Set(SETTING_DINOJOINCODE, JoinCode, EOnlineComparisonOp::Equals);

	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleFindComplete));

	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Search.Reset();
		PendingJoinCode.Reset();
		DinoScreenError(TEXT("Session search was rejected."));
		OnJoinComplete.Broadcast(false);
	}
}

// --- Console scaffolding ---------------------------------------------------------------------
// Implementations only; the exec entry points are on ADinoPlayerController. See the header for
// why. Delete this block, those exec functions, and their forwarders together once a real join
// UI exists.

void UDinoSessionSubsystem::ConsoleHost(const FString& MapName, int32 MaxPlayers)
{
	const int32 Slots = MaxPlayers > 0 ? MaxPlayers : 4;
	DinoScreenLog(FString::Printf(TEXT("DinoHost: %d slots, %s."), Slots,
		IsLANMode() ? TEXT("LAN") : TEXT("online")));

	HostSession(MapName, Slots, false);
}

void UDinoSessionSubsystem::ConsoleFind()
{
	DinoScreenLog(FString::Printf(TEXT("DinoFind: searching (%s)..."), IsLANMode() ? TEXT("LAN") : TEXT("online")));
	FindSessions(50);
}

void UDinoSessionSubsystem::ConsoleJoin(int32 SessionIndex)
{
	if (!Search.IsValid())
	{
		DinoScreenError(TEXT("DinoJoin: no search results - run DinoFind first."));
		return;
	}

	if (!Search->SearchResults.IsValidIndex(SessionIndex))
	{
		DinoScreenError(FString::Printf(TEXT("DinoJoin: index %d out of range (%d result(s))."),
			SessionIndex, Search->SearchResults.Num()));
		return;
	}

	DinoScreenLog(FString::Printf(TEXT("DinoJoin: joining index %d..."), SessionIndex));
	JoinFoundSession(SessionIndex);
}

void UDinoSessionSubsystem::ConsoleLeave()
{
	DinoScreenLog(TEXT("DinoLeave: requested."));
	LeaveSession();
}

void UDinoSessionSubsystem::ConsoleNetStatus()
{
	const IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;

	// 60s: this is a whole block to read, and on a second machine there is no scrollback to
	// recover it from once it fades.
	const float StatusDuration = 60.0f;

	DinoScreenLog(TEXT("--- Dino net status ---"), FColor::Cyan, StatusDuration);
	// First line on purpose: when two players disagree about behaviour, the first thing worth
	// ruling out is that they are on different builds.
	DinoScreenLog(FString::Printf(TEXT("  build       : %s"), *UDinoBuildInfo::GetBuildVersion()), FColor::White, StatusDuration);
	DinoScreenLog(FString::Printf(TEXT("  backend     : %s"),
		Subsystem ? *Subsystem->GetSubsystemName().ToString() : TEXT("<none>")), FColor::White, StatusDuration);
	DinoScreenLog(FString::Printf(TEXT("  LAN fallback: %s"), IsLANMode() ? TEXT("yes") : TEXT("no")), FColor::White, StatusDuration);
	DinoScreenLog(FString::Printf(TEXT("  in session  : %s"), IsInSession() ? TEXT("yes") : TEXT("no")), FColor::White, StatusDuration);

	if (!World)
	{
		DinoScreenLog(TEXT("  world       : <none>"), FColor::White, StatusDuration);
		return;
	}

	const TCHAR* NetModeText = TEXT("Client");
	switch (World->GetNetMode())
	{
	case NM_Standalone:       NetModeText = TEXT("Standalone"); break;
	case NM_ListenServer:     NetModeText = TEXT("ListenServer"); break;
	case NM_DedicatedServer:  NetModeText = TEXT("DedicatedServer"); break;
	default: break;
	}

	DinoScreenLog(FString::Printf(TEXT("  net mode    : %s"), NetModeText), FColor::White, StatusDuration);
	DinoScreenLog(FString::Printf(TEXT("  map         : %s"), *UWorld::RemovePIEPrefix(World->GetMapName())), FColor::White, StatusDuration);

	if (const UNetDriver* Driver = World->GetNetDriver())
	{
		// The line the whole command exists for: SteamSocketsNetDriver means traffic is going
		// over Steam's relay, IpNetDriver means it silently fell back to raw IP.
		DinoScreenLog(FString::Printf(TEXT("  net driver  : %s"), *Driver->GetClass()->GetName()), FColor::Yellow, StatusDuration);
		DinoScreenLog(FString::Printf(TEXT("  connections : %d"), Driver->ClientConnections.Num()), FColor::White, StatusDuration);
	}
	else
	{
		DinoScreenLog(TEXT("  net driver  : <none>"), FColor::Yellow, StatusDuration);
	}
}
