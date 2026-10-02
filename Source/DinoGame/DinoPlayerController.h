#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DinoPlayerController.generated.h"

UCLASS()
class DINOGAME_API ADinoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Called on the owning client once the pawn exists and is possessed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|Multiplayer")
	void OnLocalPawnReady(APawn* NewPawn);

	/**
	 * Blueprint widget used for the host/join menu. Must derive from UDinoJoinMenu. Set this on
	 * BP_FirstPersonPlayerController - left empty, the menu simply reports that it is unset
	 * rather than silently doing nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|UI")
	TSubclassOf<class UDinoJoinMenu> JoinMenuClass;

	/**
	 * The menu key (Tab, bound in Blueprint). Opens the in-session menu during a match - lobby,
	 * round, round over - and the host/join menu anywhere else. Does nothing on the main menu,
	 * where the host/join menu is always up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dino|UI")
	void ToggleJoinMenu();

	/** Sent by ADinoMenuGameMode: this player is on the main menu. */
	UFUNCTION(Client, Reliable)
	void ClientShowMainMenu();

	// --- Rounds ------------------------------------------------------------------------------
	// Host requests. Server RPCs so the session menu can call them the same way on every machine,
	// and each one re-checks on the server that the caller really is the host - a client whose
	// UI was tampered with, or simply out of date, must not be able to restart the round.

	UFUNCTION(Server, Reliable)
	void ServerStartRound();

	UFUNCTION(Server, Reliable)
	void ServerRestartRound();

	UFUNCTION(Server, Reliable)
	void ServerReturnToLobby();

	// --- Console scaffolding ---------------------------------------------------------------
	// TODO(join-ui): delete this whole block, its implementations, and UDinoSessionSubsystem's
	// Console* functions once a real join UI exists. Temporary until the join model is decided.
	// These forward straight to UDinoSessionSubsystem, which holds the actual logic.
	//
	// They live on the PlayerController rather than the subsystem because ULocalPlayer routes
	// console exec commands to the PlayerController first; GameInstance subsystems are not
	// reached by the in-game console in a packaged build. The console is compiled out of
	// Shipping, so these require a Development build.

	// Note on defaults: the console's argument parser rejects an omitted parameter unless the
	// function carries non-empty CPP_Default_<name> metadata. Numeric defaults survive that
	// ("4"), but an empty-string default for a map name does not — it is indistinguishable from
	// having no default. Hence the parameterless DinoHost rather than one function with both
	// arguments optional.

	/** Hosts the current map with 4 slots. The usual case. */
	UFUNCTION(Exec)
	void DinoHost();

	/** Hosts an explicit map, e.g. "DinoHostMap Lvl_FirstPerson 4". */
	UFUNCTION(Exec)
	void DinoHostMap(const FString& MapName, int32 MaxPlayers = 4);

	UFUNCTION(Exec)
	void DinoFind();

	/** Index comes from the list DinoFind prints to the log. */
	UFUNCTION(Exec)
	void DinoJoin(int32 SessionIndex = 0);

	UFUNCTION(Exec)
	void DinoLeave();

	/** Opens the host/join menu. Same thing ToggleJoinMenu does, reachable from the console. */
	UFUNCTION(Exec)
	void DinoMenu();

	/** Prints the join code for the session this player is hosting. */
	UFUNCTION(Exec)
	void DinoCode();

	/** Joins by code, e.g. "DinoJoinCode K7M2PQ". Case and separators are ignored. */
	UFUNCTION(Exec)
	void DinoJoinCode(const FString& JoinCode);

	/** Prints backend, net mode, and — the point of it — which net driver actually got used. */
	UFUNCTION(Exec)
	void DinoNetStatus();

	/** Prints whether voice is enabled, who is registered as a talker, and the engine's own dump. */
	UFUNCTION(Exec)
	void DinoVoiceStatus();

	/** Lists every AI creature with its awareness, target, and last known location. */
	UFUNCTION(Exec)
	void DinoAIStatus();

	/** Validates every piece a working creature needs, reporting each pass or fail. */
	UFUNCTION(Exec)
	void DinoAICheck();

	/** Prints the last dozen search legs, so a fast-scrolling readout can be read after the fact. */
	UFUNCTION(Exec)
	void DinoAIHistory();

	/** Toggles the state-coloured debug capsules over every creature. */
	UFUNCTION(Exec)
	void DinoAIDebug();

	/**
	 * Forces every creature into a state and locks it there, so one state can be tested
	 * without perception overwriting it. Partial names work: "hunt", "search", "sus".
	 * Pass "auto" or "release" to hand control back to perception.
	 */
	UFUNCTION(Exec)
	void DinoSetState(const FString& DesiredState);

	/**
	 * Hits the nearest living creature for Damage, e.g. "DinoHitDino 25".
	 *
	 * TODO(combat): stand-in for player attacks, which do not exist yet. Goes through
	 * UGameplayStatics::ApplyDamage - exactly the path a weapon will use - so everything behind
	 * it (stagger, pin release, creature death) is exercised for real, and a weapon replaces
	 * only this command.
	 */
	UFUNCTION(Exec)
	void DinoHitDino(float Damage = 25.0f);

	/** Re-shows the build version on screen. Not scaffolding — playtesters need this. */
	UFUNCTION(Exec)
	void DinoBuildVersion();

	/** Host: starts the round from the lobby. Same as the Start Round button. */
	UFUNCTION(Exec)
	void DinoStartRound();

	/** Host: reloads the map straight into a new round. */
	UFUNCTION(Exec)
	void DinoRestartRound();

	/** Host: reloads the map into the lobby. */
	UFUNCTION(Exec)
	void DinoLobby();

protected:
	virtual void BeginPlay() override;

	/**
	 * Runs on whichever machine the state changed on - the server for everyone, and also the
	 * owning client after ClientGotoState. Only the local half does anything here.
	 */
	virtual void BeginSpectatingState() override;
	virtual void AcknowledgePossession(APawn* NewPawn) override;
	virtual void ClientVoiceHandshakeComplete() override;

private:
	class UDinoSessionSubsystem* GetSessionSubsystem() const;

	/** Created on first use and kept, so a reopened menu is not rebuilt from scratch. */
	UPROPERTY(Transient)
	TObjectPtr<class UDinoJoinMenu> JoinMenu;

	UPROPERTY(Transient)
	TObjectPtr<class UDinoSessionMenu> SessionMenu;

	UPROPERTY(Transient)
	TObjectPtr<class UDinoSpectatorOverlay> SpectatorOverlay;

	void ShowMainMenu();
	void OpenSessionMenu();
	void CloseSessionMenu();

	/**
	 * Keeps this player's screens in step with the match, a few times a second.
	 *
	 * Polled rather than driven by replication callbacks. On a client the game state can arrive
	 * after this controller begins play, and a match-state change can land before or after a
	 * travel finishes; a callback subscribed at the wrong moment simply never fires. Reading the
	 * state on a timer cannot miss a phase.
	 *
	 * Opens the session menu on entering the lobby or round over, closes it when a round starts,
	 * and shows the spectator overlay whenever spectating a round.
	 */
	void PollSessionUI();

	FTimerHandle SessionUITimer;
	FName LastSeenMatchState;
	bool bOnMainMenu = false;

	/**
	 * Starts transmitting for the local player. Open mic: the engine gates transmission on input
	 * level (voice.SilenceDetectionThreshold), so there is no key to hold — this just opens the
	 * channel once and leaves it open. Safe to call more than once.
	 */
	void EnableOpenMic();

	/**
	 * Puts the build version on screen and leaves it there, so a tester can read or
	 * screenshot it at any point without being told how.
	 *
	 * Called from BeginPlay rather than once at startup because the engine clears on-screen
	 * messages on level load — and hosting or joining both travel, which is exactly when
	 * knowing the build matters most.
	 */
	void ShowBuildVersionOnScreen();
};
