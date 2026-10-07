#include "AI/DinoActionTasks.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoIdleComponent.h"
#include "DinoGame.h"
#include "StateTreeExecutionContext.h"

namespace
{
	template <typename TaskType>
	ADinoAIControllerBase* ResolveController(FStateTreeExecutionContext& Context, const TaskType& Task)
	{
		const FDinoActionTaskInstanceData& InstanceData = Context.GetInstanceData(Task);
		return Cast<ADinoAIControllerBase>(InstanceData.AIController);
	}

	UDinoIdleComponent* ResolveIdle(FStateTreeExecutionContext& Context, const FDinoIdleTask& Task)
	{
		const ADinoAIControllerBase* Controller = ResolveController(Context, Task);
		return Controller ? Controller->GetIdleComponent() : nullptr;
	}
}

EStateTreeRunStatus FDinoIdleTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UDinoIdleComponent* Idle = ResolveIdle(Context, *this);
	if (!Idle)
	{
		UE_LOG(LogDinoGame, Warning, TEXT("Dino Idle: no DinoAIControllerBase in context."));
		return EStateTreeRunStatus::Failed;
	}

	Idle->BeginIdle();
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDinoIdleTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	if (UDinoIdleComponent* Idle = ResolveIdle(Context, *this))
	{
		Idle->TickIdle(DeltaTime);
	}

	return EStateTreeRunStatus::Running;
}

void FDinoIdleTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Sustained means the state was reselected while staying active; it keeps wandering.
	if (Transition.ChangeType != EStateTreeStateChangeType::Changed)
	{
		return;
	}

	if (UDinoIdleComponent* Idle = ResolveIdle(Context, *this))
	{
		Idle->EndIdle();
	}
}

EStateTreeRunStatus FDinoAttackTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	ADinoAIControllerBase* Controller = ResolveController(Context, *this);
	if (!Controller)
	{
		// The AIController slot is filled by the schema's context, so an empty one means the
		// tree is running under the wrong schema rather than a transient timing problem.
		UE_LOG(LogDinoGame, Warning, TEXT("Dino Attack: no DinoAIControllerBase in context."));
		return EStateTreeRunStatus::Failed;
	}

	// In normal play the controller has already started the attack; only a debug-forced state
	// arrives here with nothing underway.
	if (Controller->IsAwarenessLocked() && !Controller->IsEngagedInAttack())
	{
		Controller->TryStartForcedAttack();
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDinoAttackTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	ADinoAIControllerBase* Controller = ResolveController(Context, *this);
	if (Controller && Controller->IsAwarenessLocked() && !Controller->IsEngagedInAttack())
	{
		// Refused while on cooldown, so this naturally paces itself.
		Controller->TryStartForcedAttack();
	}

	return EStateTreeRunStatus::Running;
}

void FDinoAttackTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Sustained means the state was reselected while staying active - the attack continues.
	if (Transition.ChangeType != EStateTreeStateChangeType::Changed)
	{
		return;
	}

	// Leaving Attacking while an attack is still running only happens from outside the attack:
	// a debug command forcing another state, or the tree being stopped. Normal finishes set
	// awareness after the component is already free, so this is a no-op for them.
	if (ADinoAIControllerBase* Controller = ResolveController(Context, *this))
	{
		if (Controller->IsEngagedInAttack())
		{
			Controller->CancelAttackFromStateTree();
		}
	}
}
