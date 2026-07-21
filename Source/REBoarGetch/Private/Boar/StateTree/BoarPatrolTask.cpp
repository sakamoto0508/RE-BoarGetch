#include "Boar/StateTree/BoarPatrolTask.h"

#include "Boar/BoarBase.h"
#include "NavigationSystem.h"

#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"

EStateTreeRunStatus FStateTreeBoarPatrolTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	
	// StateTreeを実行しているイノシシを取得する。
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	if (Boar == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// このワールドで経路探索を担当するNavigationSystemを取得する。
	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(Boar->GetWorld());
	if (NavigationSystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	FNavLocation PatrolNavLocation;

	// 現在地の周辺から、NavMesh経由で到達可能な地点だけを候補にする。
	const bool bFoundPatrolLocation = NavigationSystem->GetRandomReachablePointInRadius(
			Boar->GetActorLocation(), PatrolRadius, PatrolNavLocation);
	if (!bFoundPatrolLocation)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 後続のStateTree Move Toタスクが使用する目的地を設定する。
	InstanceData.PatrolLocation = PatrolNavLocation.Location;

	return EStateTreeRunStatus::Succeeded;
}
