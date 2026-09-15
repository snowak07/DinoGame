#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "DinoAITypes.generated.h"

/**
 * Shared awareness vocabulary for every creature, whatever its species or behaviour.
 *
 * Decided on the server and replicated to clients on the pawn, because AI controllers
 * exist only on the server and never replicate. Clients use this to drive cosmetics —
 * music, roars, animation — without re-running any AI logic, which would diverge.
 *
 * Kept deliberately small. Species-specific nuance belongs in that species' StateTree,
 * not in more enum values; every creature has to map onto these five for shared systems
 * (audio, UI, analytics) to mean anything.
 */
UENUM(BlueprintType)
enum class EDinoAwareness : uint8
{
	/** Idle behaviour: patrol, graze, wander. Nothing of interest. */
	Unaware		UMETA(DisplayName = "Unaware"),

	/** Something was sensed but not identified — a noise, a glimpse. Investigating. */
	Suspicious	UMETA(DisplayName = "Suspicious"),

	/** Target confirmed and currently perceived. */
	Alerted		UMETA(DisplayName = "Alerted"),

	/** Actively closing on a perceived target. */
	Hunting		UMETA(DisplayName = "Hunting"),

	/**
	 * Target was lost and is being searched for around its last known position.
	 *
	 * The state that makes an unfightable predator a game rather than a cutscene: it is
	 * what breaking line of sight buys the player. Skipping it means being seen is
	 * always fatal.
	 */
	Searching	UMETA(DisplayName = "Searching")
};

/**
 * One tag per EDinoAwareness value, sent to the creature's StateTree when awareness
 * changes.
 *
 * Events rather than polling, deliberately. A StateTree transition that re-evaluates an
 * enum every tick will re-enter the state it is already in and restart its task, so a
 * Move To would reset its path continuously and the creature would stand still while
 * looking busy. An event fires once, on the frame the change actually happens.
 */
namespace DinoAwarenessTags
{
	DINOGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unaware);
	DINOGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Suspicious);
	DINOGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Alerted);
	DINOGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Hunting);
	DINOGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Searching);

	/** Tag matching an awareness value, or an invalid tag if somehow unmapped. */
	DINOGAME_API FGameplayTag FromAwareness(EDinoAwareness Awareness);
}

/** Debug colour for an awareness value. Shared so every debug surface agrees. */
DINOGAME_API FColor DinoAwarenessColor(EDinoAwareness Awareness);

/** Short display name, e.g. "Hunting". */
DINOGAME_API FString DinoAwarenessName(EDinoAwareness Awareness);

/**
 * Parses a state name typed at the console. Case-insensitive, accepts partial names, so
 * "hunt" resolves to Hunting. Returns false when nothing matches.
 */
DINOGAME_API bool DinoAwarenessFromString(const FString& Text, EDinoAwareness& OutAwareness);
