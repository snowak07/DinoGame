#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "DinoSessionSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FDinoSessionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Sessions")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Sessions")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Sessions")
	FString MapName;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Sessions")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Sessions")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Sessions")
	int32 PingMs = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDinoSessionOpResult, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDinoSessionSearchResult, bool, bWasSuccessful, const TArray<FDinoSessionInfo>&, Sessions);

/**
 * Player-hosted session management, wrapping IOnlineSession so gameplay never references a
 * specific backend. Steam today; swapping DefaultPlatformService is enough to move to EOS.
 */
UCLASS()
class DINOGAME_API UDinoSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Creates a session and, on success, server-travels to MapName as a listen server. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Sessions")
	void HostSession(const FString& MapName, int32 MaxPlayers = 4, bool bPrivate = false);

	UFUNCTION(BlueprintCallable, Category = "Dino|Sessions")
	void FindSessions(int32 MaxResults = 50);

	/** Index comes from the FDinoSessionInfo array delivered by OnFindComplete. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Sessions")
	void JoinFoundSession(int32 SessionIndex);

	UFUNCTION(BlueprintCallable, Category = "Dino|Sessions")
	void LeaveSession();

	UFUNCTION(BlueprintPure, Category = "Dino|Sessions")
	bool IsInSession() const;

	/** True when no online backend is present, so host/find fall back to LAN broadcast. */
	UFUNCTION(BlueprintPure, Category = "Dino|Sessions")
	bool IsLANMode() const;

	UPROPERTY(BlueprintAssignable, Category = "Dino|Sessions")
	FDinoSessionOpResult OnHostComplete;

	UPROPERTY(BlueprintAssignable, Category = "Dino|Sessions")
	FDinoSessionSearchResult OnFindComplete;

	UPROPERTY(BlueprintAssignable, Category = "Dino|Sessions")
	FDinoSessionOpResult OnJoinComplete;

	UPROPERTY(BlueprintAssignable, Category = "Dino|Sessions")
	FDinoSessionOpResult OnLeaveComplete;

	// --- Console scaffolding ---------------------------------------------------------------
	// TODO(join-ui): delete alongside ADinoPlayerController's exec functions once a real join UI
	// exists. Temporary until the join model is decided.
	//
	// These are the implementations only. The UFUNCTION(Exec) entry points live on
	// ADinoPlayerController because the in-game console does not route exec commands to
	// GameInstance subsystems in a packaged build — declaring Exec here compiles and links
	// cleanly but yields "Command not recognized" at runtime.

	/** Hosts MapName, or the current map when empty. MaxPlayers falls back to 4 when <= 0. */
	void ConsoleHost(const FString& MapName, int32 MaxPlayers);

	void ConsoleFind();

	/** Index comes from the list ConsoleFind prints to the log. */
	void ConsoleJoin(int32 SessionIndex);

	void ConsoleLeave();

	/** Prints backend, net mode, and — the point of it — which net driver actually got used. */
	void ConsoleNetStatus();

private:
	IOnlineSessionPtr GetSessions() const;

	void CreateSessionNow();
	void JoinSessionNow(int32 SessionIndex);

	void HandleCreateComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindComplete(bool bWasSuccessful);
	void HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroyComplete(FName SessionName, bool bWasSuccessful);

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FDelegateHandle DestroyHandle;

	TSharedPtr<FOnlineSessionSearch> Search;

	/** What HandleDestroyComplete should do once a stale session has been cleared. */
	enum class EPendingAction : uint8
	{
		None,
		Rehost,
		Join,
	};

	/** Request held across the destroy-then-retry cycle used to clear a stale session. */
	FString PendingMapName;
	int32 PendingMaxPlayers = 0;
	bool bPendingPrivate = false;
	EPendingAction PendingAction = EPendingAction::None;
	int32 PendingJoinIndex = INDEX_NONE;
};
