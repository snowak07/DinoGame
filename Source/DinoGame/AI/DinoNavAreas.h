#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "DinoNavAreas.generated.h"

/**
 * The ground under a bush (ADinoBush). Walkable, but costly: a creature wandering or searching
 * takes a reasonable detour around a bush rather than through it, and only goes in when there
 * is no other way. A dino blundering through a bush by chance found whoever hid there with no
 * warning; going around leaves finding them to a deliberate check (the search's bush step).
 *
 * An area rather than a hole in the navmesh: a bush that cut the navmesh left gaps a creature
 * could not reach a player in (CLAUDE.md, Creature senses).
 */
UCLASS()
class DINOGAME_API UDinoNavArea_Foliage : public UNavArea
{
	GENERATED_BODY()

public:
	/**
	 * How many times longer a path through a bush counts than the same path in the open. 5: a
	 * detour of up to about four bush-widths is preferred to going through.
	 */
	static constexpr float FoliageCost = 5.0f;

	UDinoNavArea_Foliage();
};

/**
 * The path filter for a chase: bushes cost the same as open ground, so a creature hunting someone
 * it can see goes straight through after them.
 */
UCLASS()
class DINOGAME_API UDinoNavFilter_Chase : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UDinoNavFilter_Chase();
};
