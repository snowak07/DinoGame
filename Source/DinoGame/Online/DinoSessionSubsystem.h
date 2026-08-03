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
	// Temporary, until the join model is decided and a real UI exists. Kept here rather than on
	// the PlayerController so the whole debug surface disappears with one delete of this block.
	// The console is unavailable in Shipping, so package Development to use these.

	/** Hosts MapName, or the current map when omitted. MaxPlayers defaults to 4 when <= 0. */
	UFUNCTION(Exec)
	void DinoHost(const FString& MapName, int32 MaxPlayers);

	UFUNCTION(Exec)
	void DinoFind();

	/** Index comes from the list DinoFind prints to the log. */
	UFUNCTION(Exec)
	void DinoJoin(int32 SessionIndex);

	UFUNCTION(Exec)
	void DinoLeave();

	/** Prints backend, net mode, and — the point of it — which net driver actually got used. */
	UFUNCTION(Exec)
	void DinoNetStatus();

private:
	IOnlineSessionPtr GetSessions() const;

	void CreateSessionNow();

	void HandleCreateComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindComplete(bool bWasSuccessful);
	void HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroyComplete(FName SessionName, bool bWasSuccessful);

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FDelegateHandle DestroyHandle;

	TSharedPtr<FOnlineSessionSearch> Search;

	/** Host request held across the destroy-then-create cycle used to clear a stale session. */
	FString PendingMapName;
	int32 PendingMaxPlayers = 0;
	bool bPendingPrivate = false;
	bool bDestroyingToRehost = false;
};
