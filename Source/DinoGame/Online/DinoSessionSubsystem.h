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
UCLASS(Config = Game)
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

	/**
	 * Leaves any session and opens the main menu. What the session menu's Leave button does.
	 * Works with no session too, as in solo PIE, where there is nothing to leave.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dino|Sessions")
	void ReturnToMainMenu();

	/**
	 * The map hosting travels to when no map is named.
	 *
	 * Configured rather than read from the current world because hosting now starts on the
	 * main menu, and "the map already loaded" would host the menu itself. Override in
	 * DefaultGame.ini under [/Script/DinoGame.DinoSessionSubsystem].
	 */
	UPROPERTY(Config)
	FString GameplayMap = TEXT("/Game/FirstPerson/Lvl_FirstPerson");

	UFUNCTION(BlueprintPure, Category = "Dino|Sessions")
	bool IsInSession() const;

	/** True when no online backend is present, so host/find fall back to LAN broadcast. */
	UFUNCTION(BlueprintPure, Category = "Dino|Sessions")
	bool IsLANMode() const;

	// --- Join codes ------------------------------------------------------------------------
	// A short code is the whole join model: the host reads it out, the joiner types it. It is
	// published as a session setting and used as a search filter, so the joiner never sees a
	// list and never has to recognise their own session among strangers' AppID 480 lobbies.

	/** The code for the session this player is hosting. Empty when not hosting. */
	UFUNCTION(BlueprintPure, Category = "Dino|Sessions")
	FString GetCurrentJoinCode() const { return CurrentJoinCode; }

	/**
	 * Finds and joins the session advertising JoinCode. Case and separators are ignored, so
	 * "abc-def" and "ABCDEF" both work — players retype these from memory or from voice.
	 * Reports through OnJoinComplete like any other join.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dino|Sessions")
	void JoinByCode(const FString& JoinCode);

	/** Uppercases and strips anything outside the code alphabet. Exposed for input validation. */
	UFUNCTION(BlueprintPure, Category = "Dino|Sessions")
	static FString NormaliseJoinCode(const FString& Raw);

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

	/** Opens the game's default map, which is the main menu. No-op when already there. */
	void OpenMainMenu();

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

	/** Six characters from an alphabet with no visually or audibly ambiguous symbols. */
	static FString GenerateJoinCode();

	/** Kicks off a search filtered to a single join code. */
	void FindSessionByCode(const FString& JoinCode);

	/** Code advertised by the session we are hosting. */
	FString CurrentJoinCode;

	/**
	 * Set while a JoinByCode search is in flight. Non-empty means HandleFindComplete should
	 * join the result itself rather than just reporting the list to the UI.
	 */
	FString PendingJoinCode;

	/** Request held across the destroy-then-retry cycle used to clear a stale session. */
	FString PendingMapName;
	int32 PendingMaxPlayers = 0;
	bool bPendingPrivate = false;
	EPendingAction PendingAction = EPendingAction::None;
	int32 PendingJoinIndex = INDEX_NONE;
};
