#include "AI/DinoMoveTasks.h"

#include "AI/DinoAIControllerBase.h"
#include "DinoGame.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

namespace
{
	/**
	 * The same request the stock Move To builds, from the same node settings, so the chase
	 * honours whatever is set on the node in the tree. Always aimed at an actor, so path
	 * following re-plans as the target moves.
	 */
	FAIMoveRequest MakeChaseRequest(const FStateTreeMoveToTaskInstanceData& Settings,
		const ADinoAIControllerBase& Controller, AActor* Target)
	{
		FAIMoveRequest Request;
		Request.SetNavigationFilter(Settings.FilterClass ? Settings.FilterClass : Controller.GetDefaultNavigationFilterClass())
			.SetAllowPartialPath(Settings.bAllowPartialPath)
			.SetAcceptanceRadius(Settings.AcceptableRadius)
			.SetCanStrafe(Settings.bAllowStrafe)
			.SetReachTestIncludesAgentRadius(Settings.bReachTestIncludesAgentRadius)
			.SetReachTestIncludesGoalRadius(Settings.bReachTestIncludesGoalRadius)
			.SetRequireNavigableEndLocation(Settings.bRequireNavigableEndLocation)
			.SetProjectGoalLocation(Settings.bProjectGoalLocation)
			.SetUsePathfinding(true);
		Request.SetGoalActor(Target);
		return Request;
	}
}

EStateTreeRunStatus FDinoMoveToTargetTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	ADinoAIControllerBase* Controller = Cast<ADinoAIControllerBase>(InstanceData.AIController);
	if (!Controller)
	{
		// The AIController slot is filled by the schema's context, so an empty one means the
		// tree is running under the wrong schema rather than a transient timing problem.
		UE_LOG(LogDinoGame, Warning, TEXT("Dino Move To Current Target: no DinoAIControllerBase in context."));
		return EStateTreeRunStatus::Failed;
	}

	// Super::EnterState is deliberately not called: it starts a single move whose end
	// completes the state. See the class comment.
	//
	// No target yet is not a failure either. Failing would complete the state and drop the
	// tree to Root - the very stall this task exists to prevent. Tick picks up a target as
	// soon as the controller has one.
	if (AActor* Target = Controller->GetCurrentTarget())
	{
		InstanceData.TargetActor = Target;
		Controller->MaintainChase(MakeChaseRequest(InstanceData, *Controller, Target), /*bForceNewMove*/ true);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDinoMoveToTargetTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Not Super::Tick, which reports Failed whenever the stock move task is absent - and here it
	// always is.
	ADinoAIControllerBase* Controller = Cast<ADinoAIControllerBase>(InstanceData.AIController);
	if (Controller)
	{
		if (AActor* Target = Controller->GetCurrentTarget())
		{
			InstanceData.TargetActor = Target;

			// Cheap when nothing needs doing: the controller only issues a move when the last
			// one has ended or the target has changed.
			Controller->MaintainChase(MakeChaseRequest(InstanceData, *Controller, Target), /*bForceNewMove*/ false);
		}
	}

	return EStateTreeRunStatus::Running;
}

void FDinoMoveToTargetTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Sustained means the state was reselected while staying active; the chase carries on.
	if (Transition.ChangeType != EStateTreeStateChangeType::Changed)
	{
		return;
	}

	if (ADinoAIControllerBase* Controller = Cast<ADinoAIControllerBase>(Context.GetInstanceData(*this).AIController))
	{
		Controller->StopChase();
	}
}

bool FDinoMoveToTargetTask::Link(FStateTreeLinker& Linker)
{
	const bool bResult = Super::Link(Linker);
	bShouldCallTick = true;
	return bResult;
}

EStateTreeRunStatus FDinoMoveToLastKnownTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const ADinoAIControllerBase* Controller = Cast<ADinoAIControllerBase>(InstanceData.AIController);
	if (!Controller)
	{
		UE_LOG(LogDinoGame, Warning, TEXT("Dino Move To Last Known Location: no DinoAIControllerBase in context."));
		return EStateTreeRunStatus::Failed;
	}

	// Destination rather than TargetActor: the point is to go where the target *was*, which
	// is exactly the information a live actor reference would throw away.
	InstanceData.TargetActor = nullptr;
	InstanceData.Destination = Controller->GetLastKnownLocation();

	return Super::EnterState(Context, Transition);
}

EStateTreeRunStatus FDinoSearchStepTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	ADinoAIControllerBase* Controller = Cast<ADinoAIControllerBase>(InstanceData.AIController);
	if (!Controller)
	{
		UE_LOG(LogDinoGame, Warning, TEXT("Dino Search Step: no DinoAIControllerBase in context."));
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.TargetActor = nullptr;
	InstanceData.Destination = Controller->GetNextSearchPoint();

	// Recorded before the prefetch below overwrites the candidate, so debug draw shows the
	// destination being walked to rather than the one queued behind it.
	Controller->SetActiveSearchDestination(InstanceData.Destination);

	// Logged with the distance because the two failure modes look identical in game: a leg of
	// ~0 means the point is already inside the acceptance radius and the state is spinning,
	// while a long gap between these lines means a leg is stalling rather than completing.
	const float LegDistance = Controller->GetPawn()
		? FVector::Dist2D(Controller->GetPawn()->GetActorLocation(), InstanceData.Destination)
		: -1.0f;

	UE_LOG(LogDinoGame, Log, TEXT("%s search leg -> %s (%.0f uu)"),
		*Controller->GetName(), *InstanceData.Destination.ToCompactString(), LegDistance);

	Controller->NotifySearchLegStarted();

	// Choose the next point now, while walking to this one, so the next leg starts instantly.
	Controller->RequestNextSearchPoint();

	return Super::EnterState(Context, Transition);
}

bool FDinoMoveToLastKnownTask::Link(FStateTreeLinker& Linker)
{
	// After Super, which sets bShouldCallTick from the compile-time analysis that cannot see
	// a C++-assigned Destination.
	const bool bResult = Super::Link(Linker);
	bShouldCallTick = true;
	return bResult;
}

bool FDinoSearchStepTask::Link(FStateTreeLinker& Linker)
{
	const bool bResult = Super::Link(Linker);
	bShouldCallTick = true;
	return bResult;
}

EStateTreeRunStatus FDinoSearchStepTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (ADinoAIControllerBase* Controller = Cast<ADinoAIControllerBase>(InstanceData.AIController))
	{
		Controller->NotifySearchTick();
	}

	return Super::Tick(Context, DeltaTime);
}
