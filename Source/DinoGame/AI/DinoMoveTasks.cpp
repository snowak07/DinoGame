#include "AI/DinoMoveTasks.h"

#include "AI/DinoAIControllerBase.h"
#include "DinoGame.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

EStateTreeRunStatus FDinoMoveToTargetTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const ADinoAIControllerBase* Controller = Cast<ADinoAIControllerBase>(InstanceData.AIController);
	if (!Controller)
	{
		// The AIController slot is filled by the schema's context, so an empty one means the
		// tree is running under the wrong schema rather than a transient timing problem.
		UE_LOG(LogDinoGame, Warning, TEXT("Dino Move To Current Target: no DinoAIControllerBase in context."));
		return EStateTreeRunStatus::Failed;
	}

	AActor* Target = Controller->GetCurrentTarget();
	if (!Target)
	{
		// Not an error: awareness can drop between the event firing and this state entering.
		// Failing lets the tree transition on rather than standing still holding a dead move.
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.TargetActor = Target;

	return Super::EnterState(Context, Transition);
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
