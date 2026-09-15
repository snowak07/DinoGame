#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/DinoAITypes.h"
#include "DinoAIControllerBase.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UStateTreeAIComponent;
class UEnvQuery;
struct FEnvQueryResult;

/**
 * Shared perception and target tracking for every creature.
 *
 * Holds no species behaviour — no patrol, no attack, no flee. It answers one question
 * and answers it the same way for every dinosaur: what do I currently know about, and
 * where do I think it is? Species behaviour consumes that from a StateTree.
 *
 * AI controllers exist only on the server and never replicate, so nothing here is
 * visible to clients. Anything a client needs must go through the pawn — see
 * ADinoCreature::SetAwareness.
 */
UCLASS(Abstract)
class DINOGAME_API ADinoAIControllerBase : public AAIController
{
	GENERATED_BODY()

public:
	ADinoAIControllerBase();

	/** The actor currently perceived, or last perceived and not yet given up on. */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	AActor* GetCurrentTarget() const { return CurrentTarget; }

	/** Where the target was last perceived. Meaningless while there has never been one. */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	FVector GetLastKnownLocation() const { return LastKnownLocation; }

	/** True while the target is actually being perceived right now, not merely remembered. */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	bool HasLiveContact() const { return bHasLiveContact; }

	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	EDinoAwareness GetAwareness() const;

	// --- Searching ------------------------------------------------------------------------

	/**
	 * Where the target probably went: last known location projected along its heading when
	 * contact was lost. Exposed for the EQS context of the same name.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	FVector GetPredictedTargetLocation() const;

	/**
	 * The point the creature is walking to right now.
	 *
	 * Distinct from the prefetched candidate below, and the one worth drawing: the prefetch is
	 * overwritten the instant a leg begins, so showing it puts the marker one destination ahead
	 * of where the creature is actually going.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	FVector GetActiveSearchDestination() const { return ActiveSearchDestination; }

	/** Current effective search radius, for debug draw. */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	float GetSearchRadiusForDebug() const { return CurrentSearchRadius(); }

	/** Recorded by the search task when it issues a move, so debug draw follows reality. */
	void SetActiveSearchDestination(const FVector& Destination);

	/** The candidate chosen for the *next* leg. Seeded to LastKnownLocation on contact loss. */
	UFUNCTION(BlueprintPure, Category = "Dino|AI")
	FVector GetNextSearchPoint() const { return NextSearchPoint; }

	/**
	 * Kicks off an async EQS query for the next place to look.
	 *
	 * Async, so the result is not ready when this returns - by design. It is called on
	 * *arrival* at the current point, so the next one is already chosen by the time the
	 * creature needs it, and the query never stalls movement.
	 *
	 * Falls back to a random reachable navmesh point when no query is assigned or the query
	 * fails, so Searching degrades to wandering rather than to standing still.
	 */
	void RequestNextSearchPoint();

	/** Convenience: sets awareness on the possessed creature. Server only. */
	UFUNCTION(BlueprintCallable, Category = "Dino|AI")
	void SetAwareness(EDinoAwareness NewAwareness);

	/** One-line summary for the DinoAIStatus console command. */
	FString DescribeState() const;

	// --- Diagnostics ----------------------------------------------------------------------
	// Counters rather than logs, because the questions that matter here are "is this happening
	// at all" and "how often", which a scrolling log answers badly and an on-screen number
	// answers instantly.

	/** Called by the search task each time a leg begins. */
	void NotifySearchLegStarted();

	/** Called by the search task every tick. If this stops rising, the task is not ticking. */
	void NotifySearchTick();

	/** Compact live readout: leg number, time in leg, tick count, last query outcome. */
	FString DescribeSearchDiagnostics() const;

	/**
	 * The last few legs, newest first.
	 *
	 * A live readout is unreadable when legs turn over in under a second, and the interesting
	 * leg is always the one that just scrolled away. This keeps them.
	 */
	void DescribeSearchHistory(TArray<FString>& OutLines) const;

	/** Validates the pieces a working creature needs, reporting each one pass or fail. */
	void RunSetupCheck(TArray<FString>& OutLines) const;

	// --- Debug state control ------------------------------------------------------------
	// Lets a state be tested on its own, before the triggers that reach it are trusted.

	/**
	 * Forces awareness and locks it, so perception cannot overwrite it.
	 *
	 * The lock is the point. Without it, forcing Searching while the creature can see you
	 * is undone on the next perception update a frame later, and the command looks broken.
	 */
	void DebugForceAwareness(EDinoAwareness NewAwareness);

	/** Releases the lock and hands control back to perception. */
	void DebugReleaseAwareness();

	bool IsAwarenessLocked() const { return bAwarenessLocked; }

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;

	/** Sight cone. Beyond LoseSightRadius a live contact becomes a memory. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "0.0"))
	float SightRadius = 4000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "0.0"))
	float LoseSightRadius = 5000.0f;

	/** Half-angle. 90 gives a 180-degree field of view. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float PeripheralVisionAngle = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "0.0"))
	float HearingRange = 6000.0f;

	/**
	 * Moves the apex of the sight cone backward for the cone test only.
	 *
	 * Without this a tall creature cannot see something at its own feet: the direction from
	 * its eyes down to an adjacent target approaches 90 degrees below forward, well outside
	 * the cone, so it loses sight of a player the moment it catches them. Pulling the apex
	 * back makes that angle shallow enough to fall inside the cone.
	 *
	 * Scale it with the creature. Small animals need little or none.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "0.0"))
	float PointOfViewBackwardOffset = 500.0f;

	/** Ignores anything closer than this to the moved-back apex, so the offset does not grant vision behind. Keep below PointOfViewBackwardOffset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "0.0"))
	float NearClippingRadius = 300.0f;

	/**
	 * Distance within which an already-seen target is seen *automatically*.
	 *
	 * Disabled by default (-1), and it should stay that way for anything the player is meant
	 * to hide from. UAISense_Sight::ShouldAutomaticallySeeTarget bypasses both the vision cone
	 * and the line-of-sight trace, so a positive value here means the creature sees through
	 * walls and behind itself anywhere inside that radius. It was added to paper over a
	 * creature losing a target at its own feet - a problem PointOfViewBackwardOffset solves
	 * properly, without breaking cover.
	 *
	 * Raise it only for creatures that are supposed to be hard to escape once noticed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception")
	float AutoSuccessRangeFromLastSeen = -1.0f;

	/**
	 * How long the creature keeps hunting after losing sight, before it drops to searching.
	 *
	 * Without this, one frame of broken line of sight ends a chase: a pillar clipped at speed,
	 * or a body that turned to follow its path and swung the target out of the sight cone.
	 * A predator that gives up the instant it cannot see you is trivially escaped by circling.
	 *
	 * Note what this buys the creature: during the window it still homes on the target's live
	 * position, not its last known one, so it is briefly allowed to track through cover. Kept
	 * short for that reason - it should read as momentum, not as x-ray vision. Set to 0 to
	 * restore the old behaviour of dropping to Searching immediately.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "0.0"))
	float HuntPersistenceAfterLosingSight = 3.0f;

	/**
	 * Whether the creature keeps its eyes on the target while chasing.
	 *
	 * The sight cone is centred on the controller's control rotation, not on the body:
	 * APawn::GetViewRotation returns the controller's rotation, and AAIController::UpdateControlRotation
	 * aims that at the focus when one is set and copies the pawn's own facing when none is.
	 * With no focus, a creature that orients to its movement looks along its path - so while
	 * it rounds terrain to reach you, it is not looking at you, and loses sight of a target
	 * standing in the open.
	 *
	 * Setting focus decouples the two: the body still turns to follow its path (bUseControllerRotationYaw
	 * is false, so the focus never rotates the body) while the eyes track the target.
	 *
	 * Consequence worth knowing: while focused, the target cannot leave the sight cone, so
	 * breaking line of sight or outrunning LoseSightRadius become the only ways to escape.
	 * That is the intent for a predator, but it is why this is a switch rather than a given.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception")
	bool bTrackTargetWithEyes = true;

	/**
	 * How long a lost target stays worth searching for before the creature gives up and
	 * returns to idle. This is the length of the player's escape window, so it is a
	 * tuning dial for tension rather than a technical constant.
	 *
	 * Timed from when searching actually begins, so HuntPersistenceAfterLosingSight does not
	 * eat into it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Perception", meta = (ClampMin = "0.0"))
	float MemoryDuration = 20.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|AI|Perception")
	TObjectPtr<UAIPerceptionComponent> Perception;

	/**
	 * Query run to choose the next search point. Leave unset and searching falls back to
	 * random reachable points, which works but does not reason about cover or your heading.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Searching")
	TObjectPtr<UEnvQuery> SearchQuery;

	/**
	 * Longest a single search leg may take before it is abandoned and a new point chosen.
	 *
	 * Without this a creature that cannot reach its destination - a point across a gap, or
	 * one it keeps sliding off - stands there until the whole search times out. The search
	 * then looks broken when it is merely stuck on one bad point.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Searching", meta = (ClampMin = "0.5"))
	float SearchLegTimeout = 5.0f;

	/**
	 * How much the search area grows with each leg, as a fraction of the base radius.
	 *
	 * A fixed radius makes a creature orbit the spot you vanished from for the whole search,
	 * which reads as "it keeps going back to the same place". Widening models the fact that
	 * you have had more time to get further away.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Searching", meta = (ClampMin = "0.0"))
	float SearchRadiusGrowthPerLeg = 0.35f;

	/** Ceiling on that growth, as a multiple of the base radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Searching", meta = (ClampMin = "1.0"))
	float SearchRadiusMaxMultiplier = 4.0f;

	/** Radius for the fallback wander when no EQS query is assigned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Searching", meta = (ClampMin = "100.0"))
	float FallbackSearchRadius = 1500.0f;

	/**
	 * How far ahead of the last known location to project the target's heading.
	 *
	 * Effectively how far the creature assumes you kept running. Too short and it searches
	 * where you were; too long and it overshoots past you entirely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Searching", meta = (ClampMin = "0.0"))
	float HeadingPredictionDistance = 1000.0f;

	/**
	 * Runs this creature's behaviour tree asset. Assign the StateTree on the Blueprint
	 * child under Dino | AI; with none assigned the creature perceives normally but never
	 * acts on it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|AI")
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;

	// --- Perception output ------------------------------------------------------------
	// Exposed as properties rather than only through the getters above, because StateTree
	// bindings read properties. These are what the behaviour tree actually consumes.

	/** The actor currently perceived, or last perceived and not yet forgotten. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|AI")
	TObjectPtr<AActor> CurrentTarget;

	/** Where the target was last perceived. Only meaningful once CurrentTarget is set. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|AI")
	FVector LastKnownLocation = FVector::ZeroVector;

	/** True while the target is perceived right now, as opposed to merely remembered. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|AI")
	bool bHasLiveContact = false;

	/** Target's normalised heading when contact was lost. Zero if it was stationary. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|AI")
	FVector LastKnownDirection = FVector::ZeroVector;

	/** Candidate for the next leg, chosen while the current one is still being walked. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|AI")
	FVector NextSearchPoint = FVector::ZeroVector;

	/** Where the creature is actually moving right now. */
	UPROPERTY(BlueprintReadOnly, Category = "Dino|AI")
	FVector ActiveSearchDestination = FVector::ZeroVector;

	/** Called when a target is seen or heard for the first time in this encounter. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|AI", meta = (DisplayName = "On Target Acquired"))
	void BP_OnTargetAcquired(AActor* Target, bool bBySight);

	/** Called when live contact is lost. LastKnownLocation is already updated. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|AI", meta = (DisplayName = "On Contact Lost"))
	void BP_OnContactLost(AActor* Target, FVector LastKnown);

	/** Called when memory expires and the creature stops caring. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|AI", meta = (DisplayName = "On Target Forgotten"))
	void BP_OnTargetForgotten();

private:
	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Fires once MemoryDuration elapses without regaining contact. */
	void ForgetTarget();

	/**
	 * Drops from hunting to searching. Deferred by HuntPersistenceAfterLosingSight rather than
	 * run straight from the perception callback, so a brief loss of sight does not end a chase.
	 */
	void BeginSearching();

	FTimerHandle LostSightTimer;

	/** The actual state change, bypassing the debug lock. */
	void ApplyAwareness(EDinoAwareness NewAwareness);

	/** While true, only the debug path may change awareness. */
	bool bAwarenessLocked = false;

	void OnSearchQueryFinished(TSharedPtr<FEnvQueryResult> Result);

	/** Random reachable point near the last known location, when EQS is unavailable. */
	bool PickFallbackSearchPoint(FVector& OutPoint) const;

	/** Stops the current move so the Search Step task completes and the next leg begins. */
	void AbandonSearchLeg();

	FTimerHandle SearchLegTimer;

	/** Legs taken since this search began. Drives the widening; reset on entering Searching. */
	int32 LegsThisSearch = 0;

	/** Current effective radius, exposed for the debug readout. */
	float CurrentSearchRadius() const;


	/**
	 * Which selector produced the point actually being walked to.
	 *
	 * Both run every leg - the fallback synchronously, EQS asynchronously afterwards - so a
	 * query that returns too late loses to the fallback without any error. Counting them is
	 * the only way to tell which one is really steering the search.
	 */
	FString NextPointSource = TEXT("none");
	FString ActivePointSource = TEXT("none");
	int32 LegsFromQuery = 0;
	int32 LegsFromFallback = 0;

	/** One committed leg, recorded when the move is actually issued. */
	struct FSearchLegRecord
	{
		int32 Leg = 0;
		FString Source;
		FString QueryOutcome;
		float DistanceFromCentre = 0.0f;
		float LegLength = 0.0f;
		float SearchRadius = 0.0f;
	};

	static constexpr int32 MaxLegHistory = 12;
	TArray<FSearchLegRecord> LegHistory;

	void RecordLeg(const FVector& Destination);

	int32 SearchLegCount = 0;
	int32 ConsecutiveFastLegs = 0;
	int32 SearchTickCount = 0;
	double LastLegStartTime = 0.0;
	FString LastQueryOutcome = TEXT("none yet");

	/** Only players are worth hunting; creatures sensing each other comes later. */
	bool IsValidTarget(const AActor* Actor) const;

	UPROPERTY(Transient)
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(Transient)
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	FTimerHandle MemoryTimer;
};
