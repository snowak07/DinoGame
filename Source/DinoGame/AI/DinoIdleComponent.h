#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DinoIdleComponent.generated.h"

class AAIController;
class APawn;

/**
 * What a creature does when it has nothing to hunt: mill about its territory.
 *
 * Strolls to a random reachable point near home, stops, looks around for a few seconds, then
 * strolls somewhere else - usually somewhere roughly the way it ended up looking, so it
 * meanders rather than pacing back and forth. Home is where it was when it first went idle,
 * which for a placed creature is where it was placed; a creature that gave up a hunt far from
 * home drifts back there by itself, because every point it picks is near home.
 *
 * Lives on the AI controller, created in C++, so every controller Blueprint has it with no
 * setup. Tune it per species by selecting the Idle component in the controller Blueprint.
 *
 * Driven entirely by the Dino Idle StateTree task, which starts it on entering the Idle
 * state and stops it on leaving, so nothing here runs outside that state. Server only, like all
 * AI; clients just see the creature move.
 *
 * Its eyes go where its body goes: with no focus set, the sight cone follows the body's facing,
 * so a creature looking around during a pause really is scanning - a player in its path as it
 * turns is seen, and one behind it while it walks is not.
 */
UCLASS(ClassGroup = (Dino), meta = (DisplayName = "Dino Idle"))
class DINOGAME_API UDinoIdleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDinoIdleComponent();

	/** Starts milling about. Begins with a pause, so a creature that just gave up a search stands and looks first. */
	void BeginIdle();

	/** Called every StateTree tick while idle. */
	void TickIdle(float DeltaTime);

	/** Stops moving and puts its walking speed back. Safe to call when not idle. */
	void EndIdle();

	bool IsIdling() const { return Phase != EPhase::Inactive; }

	/** One line for the debug label: walking or pausing, and how far or how long. */
	FString DescribeIdle() const;

	/** Home, the wander radius around it, and where it is walking to. Host only. */
	void DrawDebug(const UWorld* World, float Lifetime) const;

	/** PASS/FAIL lines for DinoAICheck. */
	void AppendSetupCheck(TArray<FString>& OutLines) const;

protected:
	/** How far from home it wanders. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "100.0"))
	float WanderRadius = 2500.0f;

	/** Shortest stroll worth taking. Stops it shuffling a step at a time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.0"))
	float MinLegDistance = 600.0f;

	/**
	 * Wander around where it was first placed (true), or around wherever it happens to be (false).
	 * False lets a creature drift across the whole map over time.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle")
	bool bStayNearHome = true;

	/**
	 * Fraction of its normal walking speed while idle. Restored the moment it stops idling, so
	 * the switch to a chase is itself a tell.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float WalkSpeedMultiplier = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.0"))
	float AcceptanceRadius = 100.0f;

	/** Shortest stop between strolls. Set both to 0 for a creature that never stops walking. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.0"))
	float PauseMinSeconds = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.0"))
	float PauseMaxSeconds = 6.0f;

	/**
	 * How far either side of the way it stopped facing it may turn to look. Under 180, so a
	 * glance never reads as spinning on the spot.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.0", ClampMax = "179.0"))
	float LookAroundAngle = 110.0f;

	/** How long it holds each look before turning to the next. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.1"))
	float GlanceMinSeconds = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "0.1"))
	float GlanceMaxSeconds = 2.2f;

	/** Degrees per second while looking around. Slower than its hunting turn reads as unhurried. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|AI|Idle", meta = (ClampMin = "1.0"))
	float GlanceTurnRate = 90.0f;

private:
	enum class EPhase : uint8
	{
		Inactive,
		Walking,
		Pausing
	};

	AAIController* GetAIController() const;
	APawn* GetPawn() const;

	/** This creature's navmesh, so wander points are ones it - not the largest creature - can reach. */
	class ANavigationData* GetNavData() const;
	double Now() const;

	void StartPause();
	void StartWalk();

	/**
	 * A random reachable point around the wander origin: at least MinLegDistance away, and
	 * preferably not behind the creature, so successive legs flow on from each other instead of
	 * doubling back. Takes the longest of a few tries if none fit.
	 */
	bool PickDestination(FVector& OutDestination) const;

	void SetIdleSpeed(bool bIdle);

	EPhase Phase = EPhase::Inactive;

	bool bHasHome = false;
	FVector Home = FVector::ZeroVector;

	FVector Destination = FVector::ZeroVector;
	double WalkGivesUpAt = 0.0;

	double PauseEndsAt = 0.0;
	double NextGlanceAt = 0.0;
	float PauseBaseYaw = 0.0f;
	float GlanceYaw = 0.0f;

	/** Walks that could not start in a row. Past a few, wander around where it stands instead of home. */
	int32 FailedWalks = 0;

	/** The speed it had before idling slowed it, put back by EndIdle. Negative while not slowed. */
	float SavedMaxWalkSpeed = -1.0f;
};
