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

	/** Prints backend, net mode, and — the point of it — which net driver actually got used. */
	UFUNCTION(Exec)
	void DinoNetStatus();

	/** Prints whether voice is enabled, who is registered as a talker, and the engine's own dump. */
	UFUNCTION(Exec)
	void DinoVoiceStatus();

protected:
	virtual void BeginPlay() override;
	virtual void AcknowledgePossession(APawn* NewPawn) override;
	virtual void ClientVoiceHandshakeComplete() override;

private:
	class UDinoSessionSubsystem* GetSessionSubsystem() const;

	/**
	 * Starts transmitting for the local player. Open mic: the engine gates transmission on input
	 * level (voice.SilenceDetectionThreshold), so there is no key to hold — this just opens the
	 * channel once and leaves it open. Safe to call more than once.
	 */
	void EnableOpenMic();
};
