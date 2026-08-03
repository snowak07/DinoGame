#include "Online/DinoSessionSubsystem.h"

#include "DinoGame.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"

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

	PendingMapName = MapName;
	PendingMaxPlayers = FMath::Max(1, MaxPlayers);
	bPendingPrivate = bPrivate;

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		bDestroyingToRehost = true;
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UDinoSessionSubsystem::HandleDestroyComplete));

		if (!Sessions->DestroySession(NAME_GameSession))
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
			bDestroyingToRehost = false;
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
		UE_LOG(LogDinoNet, Error, TEXT("Failed to create session %s."), *SessionName.ToString());
		OnHostComplete.Broadcast(false);
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		OnHostComplete.Broadcast(false);
		return;
	}

	UE_LOG(LogDinoNet, Log, TEXT("Session created. Travelling to %s as listen server."), *PendingMapName);

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

	if (bWasSuccessful)
	{
		UE_LOG(LogDinoNet, Log, TEXT("Search complete: %d session(s)."), Results.Num());
		for (const FDinoSessionInfo& Info : Results)
		{
			UE_LOG(LogDinoNet, Log, TEXT("  [%d] host=%s map=%s players=%d/%d ping=%dms"),
				Info.Index, *Info.HostName, *Info.MapName, Info.CurrentPlayers, Info.MaxPlayers, Info.PingMs);
		}
	}
	else
	{
		UE_LOG(LogDinoNet, Error, TEXT("Session search failed."));
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
		UE_LOG(LogDinoNet, Error, TEXT("Join failed for %s (result %d)."), *SessionName.ToString(), static_cast<int32>(Result));
		OnJoinComplete.Broadcast(false);
		return;
	}

	FString ConnectString;
	APlayerController* PlayerController = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!PlayerController || !Sessions->GetResolvedConnectString(SessionName, ConnectString))
	{
		UE_LOG(LogDinoNet, Error, TEXT("Joined %s but could not resolve a connect string."), *SessionName.ToString());
		OnJoinComplete.Broadcast(false);
		return;
	}

	UE_LOG(LogDinoNet, Log, TEXT("Joined %s. Travelling to %s."), *SessionName.ToString(), *ConnectString);

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

	bDestroyingToRehost = false;
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

	if (bDestroyingToRehost)
	{
		bDestroyingToRehost = false;
		if (bWasSuccessful)
		{
			CreateSessionNow();
		}
		else
		{
			OnHostComplete.Broadcast(false);
		}
		return;
	}

	UE_LOG(LogDinoNet, Log, TEXT("Left session %s (%s)."), *SessionName.ToString(),
		bWasSuccessful ? TEXT("ok") : TEXT("failed"));

	OnLeaveComplete.Broadcast(bWasSuccessful);
}

// --- Console scaffolding ---------------------------------------------------------------------
// See the header for why these live here. Delete this block and the matching UFUNCTION(Exec)
// declarations together once a real join UI exists.

void UDinoSessionSubsystem::DinoHost(const FString& MapName, int32 MaxPlayers)
{
	FString ResolvedMap = MapName;
	if (ResolvedMap.IsEmpty())
	{
		const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		ResolvedMap = World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString();
	}

	if (ResolvedMap.IsEmpty())
	{
		UE_LOG(LogDinoNet, Error, TEXT("DinoHost: could not work out which map to host."));
		return;
	}

	const int32 Slots = MaxPlayers > 0 ? MaxPlayers : 4;
	UE_LOG(LogDinoNet, Log, TEXT("DinoHost: %s, %d slots, %s."), *ResolvedMap, Slots,
		IsLANMode() ? TEXT("LAN") : TEXT("online"));

	HostSession(ResolvedMap, Slots, false);
}

void UDinoSessionSubsystem::DinoFind()
{
	UE_LOG(LogDinoNet, Log, TEXT("DinoFind: searching (%s)..."), IsLANMode() ? TEXT("LAN") : TEXT("online"));
	FindSessions(50);
}

void UDinoSessionSubsystem::DinoJoin(int32 SessionIndex)
{
	if (!Search.IsValid())
	{
		UE_LOG(LogDinoNet, Error, TEXT("DinoJoin: no search results — run DinoFind first."));
		return;
	}

	if (!Search->SearchResults.IsValidIndex(SessionIndex))
	{
		UE_LOG(LogDinoNet, Error, TEXT("DinoJoin: index %d out of range (%d result(s))."),
			SessionIndex, Search->SearchResults.Num());
		return;
	}

	UE_LOG(LogDinoNet, Log, TEXT("DinoJoin: joining index %d..."), SessionIndex);
	JoinFoundSession(SessionIndex);
}

void UDinoSessionSubsystem::DinoLeave()
{
	UE_LOG(LogDinoNet, Log, TEXT("DinoLeave: requested."));
	LeaveSession();
}

void UDinoSessionSubsystem::DinoNetStatus()
{
	const IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;

	UE_LOG(LogDinoNet, Log, TEXT("--- Dino net status ---"));
	UE_LOG(LogDinoNet, Log, TEXT("  backend     : %s"),
		Subsystem ? *Subsystem->GetSubsystemName().ToString() : TEXT("<none>"));
	UE_LOG(LogDinoNet, Log, TEXT("  LAN fallback: %s"), IsLANMode() ? TEXT("yes") : TEXT("no"));
	UE_LOG(LogDinoNet, Log, TEXT("  in session  : %s"), IsInSession() ? TEXT("yes") : TEXT("no"));

	if (!World)
	{
		UE_LOG(LogDinoNet, Log, TEXT("  world       : <none>"));
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

	UE_LOG(LogDinoNet, Log, TEXT("  net mode    : %s"), NetModeText);
	UE_LOG(LogDinoNet, Log, TEXT("  map         : %s"), *UWorld::RemovePIEPrefix(World->GetMapName()));

	if (const UNetDriver* Driver = World->GetNetDriver())
	{
		UE_LOG(LogDinoNet, Log, TEXT("  net driver  : %s"), *Driver->GetClass()->GetName());
		UE_LOG(LogDinoNet, Log, TEXT("  connections : %d"), Driver->ClientConnections.Num());
	}
	else
	{
		UE_LOG(LogDinoNet, Log, TEXT("  net driver  : <none>"));
	}
}
