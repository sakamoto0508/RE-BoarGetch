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
	// このワールドで経路探索を担当するNavigationSystemを取得する。
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Boar->GetWorld());

	if (Nav == nullptr)
		return EStateTreeRunStatus::Failed;

	// プレイヤーから逃げる方向
	const FVector AwayDirection = (Boar->GetActorLocation() -
		InstanceData.TargetPlayer->GetActorLocation()).GetSafeNormal();
	
	// DataAssetから、このイノシシ種別に設定された逃走距離を取得する。
	const float EscapeDistance = Boar->GetEscapeRange();
	if (EscapeDistance <= KINDA_SMALL_NUMBER)
		return EStateTreeRunStatus::Failed;
	
	//にげる方向に逃走距離分だけ移動した地点を計算する。
	const FVector DesiredLocation = Boar->GetActorLocation() + AwayDirection * EscapeDistance;

	FNavLocation Result;

	const bool bFound = Nav->ProjectPointToNavigation(DesiredLocation, Result);

	if (!bFound)
		return EStateTreeRunStatus::Failed;

	// NavMesh端で現在地付近へ投影された場合は、即座に移動完了しないよう失敗として扱う。
	const float MinimumEscapeDistance = FMath::Min(300.0f, EscapeDistance * 0.5f);
	if (FVector::DistSquared(Boar->GetActorLocation(), Result.Location) <
		FMath::Square(MinimumEscapeDistance))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.EscapeLocation = Result.Location;
	return EStateTreeRunStatus::Running;
}
