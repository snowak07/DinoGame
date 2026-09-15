#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "DinoEQContexts.generated.h"

/**
 * EQS ships only two contexts, Querier and Item, so anything a query needs to reason about
 * other than "me" and "this candidate point" has to be provided here.
 *
 * Both of these read from the querier's ADinoAIControllerBase, resolving whether the query
 * owner is the controller or the pawn, since EQS is called with either depending on setup.
 */

/**
 * Where the target was last perceived. The natural centre for a search query.
 */
UCLASS(DisplayName = "Dino Last Known Location")
class DINOGAME_API UDinoEQContext_LastKnownLocation : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * Where the target probably went: last known location projected along the direction it was
 * travelling when contact was lost.
 *
 * Scoring search points by distance to this, rather than to the last known location, is what
 * makes a creature search along your escape route instead of circling the spot you vanished
 * from. It is the cheapest thing that makes a search look like reasoning.
 */
UCLASS(DisplayName = "Dino Predicted Location")
class DINOGAME_API UDinoEQContext_PredictedLocation : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
