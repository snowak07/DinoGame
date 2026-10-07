#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "DinoActionTasks.generated.h"

class AAIController;

/**
 * StateTree tasks that act rather than drive a single move - attacking, idling, and the future
 * scan-pause at each search point.
 *
 * Separate from DinoMoveTasks because those all subclass FStateTreeMoveToTask and reuse its
 * instance data. These derive from FStateTreeAIActionTaskBase and declare their own, which
 * also means none of the Link() workaround those need: that exists only to defeat
 * FStateTreeMoveToTask's compile-time tick analysis, and FStateTreeNodeBase already ticks by
 * default.
 */

USTRUCT()
struct FDinoActionTaskInstanceData
{
	GENERATED_BODY()

	/** Filled in automatically by UStateTreeAIComponentSchema - anything in the Context category is. */
	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AAIController> AIController = nullptr;
};

/**
 * Holds the creature in its attack. Put this in the Attacking state.
 *
 * Deliberately thin, because the StateTree is not what starts attacks. The controller decides,
 * starts the attack component, and only then sends the Attacking event - so by the time this
 * state is entered the attack is already underway, and this task's job is to stand in place of
 * the Hunting MoveTo so nothing path-follows through the windup.
 *
 * That ordering is on purpose: attacks keep working with the StateTree unwired, and awareness
 * cannot get stuck in Attacking waiting on a task.
 *
 * Never returns Succeeded. Like every other state here, Attacking is left only on an awareness
 * event, never on completion - completion transitions are what make a state re-enter itself.
 *
 * Under a debug lock (`DinoSetState att`) it restarts attacks as the cooldown allows, so one
 * attack can be watched over and over for tuning.
 */
USTRUCT(meta = (DisplayName = "Dino Attack", Category = "Dino|AI"))
struct DINOGAME_API FDinoAttackTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FDinoActionTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/**
 * Mills about while there is nothing to hunt. Put this in the Idle state - the one the
 * Dino.Awareness.Unaware event goes to.
 *
 * The behaviour itself - stroll, stop, look around, stroll on - lives in the controller's Idle
 * component, where it is tuned per species. This task only starts it on entry, ticks it, and
 * stops it on exit, so wandering can never outlive the state.
 *
 * Never completes, for the same reason as every other state here: a completed state with no
 * completion transition falls back to Root. Unaware is left only on an awareness event.
 */
USTRUCT(meta = (DisplayName = "Dino Idle", Category = "Dino|AI"))
struct DINOGAME_API FDinoIdleTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FDinoActionTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
