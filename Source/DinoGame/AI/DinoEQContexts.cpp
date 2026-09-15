#include "AI/DinoEQContexts.h"

#include "AI/DinoAIControllerBase.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/Pawn.h"

namespace
{
	/**
	 * EQS may be handed either the controller or the pawn as the query owner, depending on how
	 * the query is invoked. Resolve both rather than assuming, since guessing wrong produces a
	 * query that silently returns nothing.
	 */
	const ADinoAIControllerBase* ResolveDinoController(const FEnvQueryInstance& QueryInstance)
	{
		UObject* Owner = QueryInstance.Owner.Get();
		if (!Owner)
		{
			return nullptr;
		}

		if (const ADinoAIControllerBase* AsController = Cast<ADinoAIControllerBase>(Owner))
		{
			return AsController;
		}

		if (const APawn* AsPawn = Cast<APawn>(Owner))
		{
			return Cast<ADinoAIControllerBase>(AsPawn->GetController());
		}

		return nullptr;
	}
}

namespace
{
	/**
	 * Falls back to the querier's own location when there is no Dino controller.
	 *
	 * That case is not a failure - it is EQSTestingPawn, which is not an AI controller and
	 * exists precisely so a query can be previewed in the level. Returning nothing there would
	 * make every test render an empty result and make the query look broken while authoring it.
	 */
	bool ResolveQuerierLocation(const FEnvQueryInstance& QueryInstance, FVector& OutLocation)
	{
		if (const AActor* OwnerActor = Cast<AActor>(QueryInstance.Owner.Get()))
		{
			OutLocation = OwnerActor->GetActorLocation();
			return true;
		}

		return false;
	}
}

void UDinoEQContext_LastKnownLocation::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	if (const ADinoAIControllerBase* Controller = ResolveDinoController(QueryInstance))
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, Controller->GetLastKnownLocation());
		return;
	}

	FVector Fallback;
	if (ResolveQuerierLocation(QueryInstance, Fallback))
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, Fallback);
	}
}

void UDinoEQContext_PredictedLocation::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	if (const ADinoAIControllerBase* Controller = ResolveDinoController(QueryInstance))
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, Controller->GetPredictedTargetLocation());
		return;
	}

	FVector Fallback;
	if (ResolveQuerierLocation(QueryInstance, Fallback))
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, Fallback);
	}
}
