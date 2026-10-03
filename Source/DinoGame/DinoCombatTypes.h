#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "DinoCombatTypes.generated.h"

class ADinoCreature;

/**
 * Where a creature's attack currently is.
 *
 * Replicated on the attack component so every machine can drive cosmetics from it - a roar on
 * windup, a music sting on a grab - without running any AI. The AI controller exists only on
 * the server, so this is the only view clients get of what a creature is doing up close.
 *
 * Shared by every attack style. Not every style uses every phase: the T-Rex devour has no
 * windup or lunge, and nothing but the lunge ever misses into Recovery.
 */
UENUM(BlueprintType)
enum class EDinoAttackPhase : uint8
{
	/** Not attacking. Free to hunt, search, or start an attack once off cooldown. */
	None		UMETA(DisplayName = "None"),

	/** Telegraphing. Stationary and turning to face the target - the window to dodge. */
	Windup		UMETA(DisplayName = "Windup"),

	/** Committed to a dash along its facing. Bites the first valid player it reaches. */
	Lunge		UMETA(DisplayName = "Lunge"),

	/** Has a victim - pinned under a raptor, or in a T-Rex's jaws. */
	Holding		UMETA(DisplayName = "Holding"),

	/** Standing still after an attack. For the lunge, the punish window after a miss. */
	Recovery	UMETA(DisplayName = "Recovery"),

	/** Knocked off its attack by a hit. Drops any victim. */
	Staggered	UMETA(DisplayName = "Staggered")
};

/** How an attack ended, reported to the controller so it can decide what to do next. */
UENUM(BlueprintType)
enum class EDinoAttackOutcome : uint8
{
	/** Lunged and caught nobody. */
	Missed		UMETA(DisplayName = "Missed"),

	/** The victim died, by this attack or otherwise. */
	Killed		UMETA(DisplayName = "Killed"),

	/** Staggered by a hit. Any victim was released. */
	Interrupted	UMETA(DisplayName = "Interrupted"),

	/** Stopped from outside - the creature died, lost its controller, or a debug command. */
	Cancelled	UMETA(DisplayName = "Cancelled")
};

/**
 * A player being held by a creature.
 *
 * Lives on the player rather than the creature because it is the player whose movement,
 * input, and eventually camera have to change - and the player's own client needs it to stop
 * predicting movement, or a pinned player rubber-bands against the server.
 */
UENUM(BlueprintType)
enum class EDinoRestraint : uint8
{
	None		UMETA(DisplayName = "None"),

	/** In a T-Rex's jaws. Unescapable; ends in death. */
	Devoured	UMETA(DisplayName = "Devoured"),

	/** Under a raptor. Takes damage over time until an ally staggers it off. */
	Pinned		UMETA(DisplayName = "Pinned")
};

/**
 * Kind and captor together, replicated as one property on purpose.
 *
 * Two separate replicated properties can have their RepNotifies fire in either order, so a
 * client could briefly see a kind with no captor, or a captor with the old kind. One struct
 * with one RepNotify means the change always arrives whole.
 */
USTRUCT(BlueprintType)
struct FDinoRestraintState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Restraint")
	EDinoRestraint Kind = EDinoRestraint::None;

	UPROPERTY(BlueprintReadOnly, Category = "Dino|Restraint")
	TObjectPtr<ADinoCreature> Captor = nullptr;

	/**
	 * Flat direction from the victim to the captor at the moment of capture. Fixed for the whole
	 * hold, and decided once on the server.
	 *
	 * Not recomputed from where the captor stands now: a pinning raptor climbs onto the body, so
	 * a minute later "toward the captor" points nowhere useful - and on a client, the pin and the
	 * raptor's move onto the body arrive in either order. Everyone lays the body out from this.
	 */
	UPROPERTY()
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;
};
