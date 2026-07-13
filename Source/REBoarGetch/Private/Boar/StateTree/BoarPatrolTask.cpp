#include "Boar/StateTree/BoarPatrolTask.h"

#include "Boar/BoarBase.h"
#include "Boar/AI/PatrolPath.h"

#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"

EStateTreeRunStatus FStateTreeBoarPatrolTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	if (Boar == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	APatrolPath* PatrolPath = Boar->GetPatrolPath();
	if (PatrolPath == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	const int32 NumPoints = PatrolPath->GetPointCount();
	if (NumPoints == 0)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.CurrentPatrolIndex >= NumPoints)
	{
		InstanceData.CurrentPatrolIndex = 0;
	}

	// 今回向かう巡回地点
	InstanceData.PatrolLocation =
		PatrolPath->GetPatrolPoint(InstanceData.CurrentPatrolIndex);

	// 次回用
	InstanceData.CurrentPatrolIndex++;

	if (InstanceData.CurrentPatrolIndex >= NumPoints)
	{
		InstanceData.CurrentPatrolIndex = 0;
	}

	return EStateTreeRunStatus::Succeeded;
}