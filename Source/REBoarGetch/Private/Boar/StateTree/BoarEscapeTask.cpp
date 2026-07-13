#include "Boar/StateTree/BoarEscapeTask.h"

#include "Boar/BoarBase.h"

#include "NavigationSystem.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FStateTreeBoarEscapeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());

	if (Boar == nullptr)
		return EStateTreeRunStatus::Failed;
	

	if (InstanceData.TargetPlayer == nullptr)
		return EStateTreeRunStatus::Failed;
	

	UNavigationSystemV1* Nav =FNavigationSystem::GetCurrent<UNavigationSystemV1>(Boar->GetWorld());

	if (Nav == nullptr)
		return EStateTreeRunStatus::Failed;
	
	// プレイヤーから逃げる方向
	const FVector AwayDirection = (Boar->GetActorLocation() -
		InstanceData.TargetPlayer->GetActorLocation()).GetSafeNormal();

	// 800cm先を目標にする
	const FVector DesiredLocation = Boar->GetActorLocation() + AwayDirection * 800.f;

	FNavLocation Result;

	const bool bFound = Nav->ProjectPointToNavigation(DesiredLocation, Result);

	if (!bFound)
		return EStateTreeRunStatus::Failed;

	InstanceData.EscapeLocation = Result.Location;

	return EStateTreeRunStatus::Succeeded;
}
