#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "DinoPlayerState.generated.h"

class ADinoCharacter;
class ADinoPlayerState;
class UDamageType;
class UDinoVoiceTalker;

/**
 * Where a player is in the round. The single answer every system reads - spectating, voice,
 * the round-end check, the Tab menu - so no two of them can disagree.
 */
UENUM(BlueprintType)
enum class EDinoPlayerStatus : uint8
{
	/** No character this round: in the lobby, or joined after the round started. */
	Lobby	UMETA(DisplayName = "Lobby"),

	/** Has a living character. */
	Alive	UMETA(DisplayName = "Alive"),

	/** Reserved for revive: down but not out. Counts as alive. Nothing sets it yet. */
	Downed	UMETA(DisplayName = "Downed"),

	/** Died this round, and is spectating until the next one. */
	Dead	UMETA(DisplayName = "Dead")
};

/**
 * Status and cause together, replicated as one property on purpose - the same reasoning as
 * FDinoRestraintState. Two separate properties can arrive in different updates, so a client
 * could see "Dead" with the previous death's cause, or a cause with no death.
 */
USTRUCT(BlueprintType)
struct FDinoPlayerStatusState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Player")
	EDinoPlayerStatus Status = EDinoPlayerStatus::Lobby;

	/** What killed them, as a damage type class. Only meaningful while Dead. See UDinoDamageType. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|Player")
	TSubclassOf<UDamageType> Cause = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDinoPlayerStatusChanged,
	ADinoPlayerState*, Player, EDinoPlayerStatus, OldStatus, EDinoPlayerStatus, NewStatus);

UCLASS()
class DINOGAME_API ADinoPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ADinoPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Dino|Player")
	EDinoPlayerStatus GetStatus() const { return StatusState.Status; }

	/** What killed this player. Null unless Dead. */
	UFUNCTION(BlueprintPure, Category = "Dino|Player")
	TSubclassOf<UDamageType> GetDeathCause() const { return StatusState.Cause; }

	/**
	 * Alive or Downed: still in the round. What the round-end check counts, and what voice and
	 * spectating mean by "living".
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|Player")
	bool IsAlive() const { return StatusState.Status == EDinoPlayerStatus::Alive || StatusState.Status == EDinoPlayerStatus::Downed; }

	/**
	 * Server only. The one place a status changes. Deaths reach it through
	 * ADinoGameMode::KillPlayer, which does the rest of the bookkeeping in order - call that,
	 * not this, to kill someone.
	 *
	 * Pushes the change out at once: a PlayerState only sends updates once a second by default,
	 * so without that a client could learn of a death a second after the body vanished.
	 */
	void SetStatus(EDinoPlayerStatus NewStatus, TSubclassOf<UDamageType> Cause = nullptr);

	/** Whether this player is the one hosting. Replicated so every lobby can mark them. */
	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	bool IsHost() const { return bIsHost; }

	/** Server only. Set once, at login. */
	void SetIsHost(bool bNewIsHost);

	/**
	 * Fires on every machine, server included, when this player's status changes. Also relayed
	 * through ADinoGameState::OnPlayerStatusChanged, which is usually the better thing to bind:
	 * it covers every player, including ones who join later.
	 *
	 * Treat it as a nudge, not the truth: anything that has to be right rebuilds from the
	 * current statuses rather than applying this one change. A late joiner, or a system that
	 * binds late, never sees the changes that came before it.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Player")
	FDinoPlayerStatusChanged OnStatusChanged;

	// --- Voice ---------------------------------------------------------------------------------
	// This player's voice as heard on this machine. On the PlayerState rather than the character
	// because it has to exist the whole session; see UDinoVoiceTalker for why that matters.

	UDinoVoiceTalker* GetVoiceTalker() const { return VoiceTalker; }

	/** The body this player's voice comes from on this machine, if any. */
	ADinoCharacter* GetVoiceBody() const { return VoiceBody.Get(); }

	/** Called by this player's character on every machine once it knows it belongs to them. */
	void SetVoiceBody(ADinoCharacter* Body);

	/** Called by this player's character as it leaves the world. Ignored if it is not the current body. */
	void ClearVoiceBody(ADinoCharacter* Body);

	/**
	 * Decides where this player's voice comes from on this machine, from scratch: their body,
	 * while they are alive and the body exists here; nowhere in particular otherwise. Dead
	 * players' voices are unpositioned, so other dead players hear them at full volume; the
	 * living do not hear them at all (ADinoGameMode's voice rules).
	 *
	 * Called whenever any input changes - status, body arriving or leaving - and cheap when
	 * nothing has: the talker only refreshes the voice if the answer is different.
	 */
	void RefreshVoicePlacement();

	/** This player's current voice level, 0 when silent. */
	UFUNCTION(BlueprintPure, Category = "Dino|Voice")
	float GetVoiceLevel() const;

protected:
	virtual void BeginPlay() override;
	virtual void OnSetUniqueId() override;

	/**
	 * Lobby by default, and Alive only once the game mode spawns a character for a round.
	 *
	 * Defaulting to Alive would count lobby players and late joiners - neither of whom has a
	 * character - as alive, and a round whose end condition is "nobody alive" could then never
	 * end.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_StatusState, BlueprintReadOnly, Category = "Dino|Player")
	FDinoPlayerStatusState StatusState;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dino|Multiplayer")
	bool bIsHost = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Voice")
	TObjectPtr<UDinoVoiceTalker> VoiceTalker;

	UFUNCTION()
	void OnRep_StatusState(const FDinoPlayerStatusState& OldState);

private:
	/** Log, broadcast and relay. Every machine, from SetStatus on the server and the RepNotify elsewhere. */
	void ApplyStatusChange(EDinoPlayerStatus OldStatus);

	/**
	 * Registers the talker as this player's voice. Needs the player's net ID, which usually
	 * arrives after BeginPlay - hence both BeginPlay and OnSetUniqueId.
	 *
	 * OnSetUniqueId, not OnRep_UniqueId: a RepNotify only fires on clients, and on the server
	 * the ID is set at login (AGameModeBase::InitNewPlayer), after the PlayerState has already
	 * begun play. Hooking only the RepNotify left every client's voice unregistered on the
	 * host, so the host heard clients at full volume from anywhere. OnSetUniqueId is called on
	 * both sides: by SetUniqueId on the server, and by OnRep_UniqueId on clients.
	 */
	void RegisterVoiceTalker();

	TWeakObjectPtr<ADinoCharacter> VoiceBody;
};
