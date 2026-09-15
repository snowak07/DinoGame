#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeMoveToTask.h"
#include "DinoMoveTasks.generated.h"

/**
 * Move To, with the destination already wired to the creature's own perception.
 *
 * These exist so a species tree does not have to hand-bind task inputs. The stock Move To
 * task needs TargetActor or Destination bound through the property picker, repeated for
 * every state in every creature, and a binding pointed at the wrong thing fails silently
 * — a creature that paths to itself looks like broken navigation, not a bad binding.
 *
 * Everything else about Move To is inherited unchanged, including Track Moving Goal.
 */
USTRUCT(meta = (DisplayName = "Dino Move To Current Target", Category = "Dino|AI"))
struct DINOGAME_API FDinoMoveToTargetTask : public FStateTreeMoveToTask
{
	GENERATED_BODY()

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** Move To, aimed at where the target was last perceived. The Searching behaviour. */
USTRUCT(meta = (DisplayName = "Dino Move To Last Known Location", Category = "Dino|AI"))
struct DINOGAME_API FDinoMoveToLastKnownTask : public FStateTreeMoveToTask
{
	GENERATED_BODY()

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	/**
	 * Forces ticking on.
	 *
	 * FStateTreeMoveToTask enables ticking only when its Destination property has a *binding*
	 * in the tree, because that is the only case the stock task needs to re-read. We set
	 * Destination from C++ instead, so that check never passes and the task never ticks - and
	 * Tick is the only place arrival is detected. The creature reaches its destination and the
	 * state simply never completes until some unrelated event wakes the tree.
	 */
	virtual bool Link(FStateTreeLinker& Linker) override;
};

/**
 * One leg of a search sweep: move to the point currently chosen, and request the next one on
 * the way.
 *
 * The prefetch is what keeps this from stalling. EQS is asynchronous, so asking for a point
 * and waiting for it would leave the creature standing still every cycle. Instead the query
 * for leg N+1 is kicked off as leg N begins, and has long since finished by the time it is
 * needed.
 *
 * Loop it by giving the Searching state a transition on completion back to itself.
 */
USTRUCT(meta = (DisplayName = "Dino Search Step", Category = "Dino|AI"))
struct DINOGAME_API FDinoSearchStepTask : public FStateTreeMoveToTask
{
	GENERATED_BODY()

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	/** Counts ticks on the controller, so "is this task ticking at all" is directly observable. */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	/**
	 * Forces ticking on.
	 *
	 * FStateTreeMoveToTask enables ticking only when its Destination property has a *binding*
	 * in the tree, because that is the only case the stock task needs to re-read. We set
	 * Destination from C++ instead, so that check never passes and the task never ticks - and
	 * Tick is the only place arrival is detected. The creature reaches its destination and the
	 * state simply never completes until some unrelated event wakes the tree.
	 */
	virtual bool Link(FStateTreeLinker& Linker) override;
};
