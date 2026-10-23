#include "Boar/StateTree/BoarChasePlayerTask.h"

#include "Boar/BoarBase.h"

#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FStateTreeBoarChasePlayerTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ChaseTarget = nullptr;
	return UpdateChase(InstanceData);
}

EStateTreeRunStatus FStateTreeBoarChasePlayerTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	(void)DeltaTime;
	return UpdateChase(Context.GetInstanceData(*this));
}

void FStateTreeBoarChasePlayerTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ChaseTarget = nullptr;
	if (const APawn* Pawn = InstanceData.NPC.Get())
	{
		if (AAIController* Controller = Cast<AAIController>(Pawn->GetController()))
		{
			Controller->StopMovement();
		}
	}
}

EStateTreeRunStatus FStateTreeBoarChasePlayerTask::UpdateChase(FInstanceDataType& InstanceData) const
{
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	AAIController* Controller = Boar ? Cast<AAIController>(Boar->GetController()) : nullptr;
	if (Boar == nullptr || Controller == nullptr || Boar->IsCaptured())
		return EStateTreeRunStatus::Failed;

	// Global Senseが更新した認識結果を使い、距離・視野角・遮蔽の既存ルールで見失いを判定する。
	APawn* Player = Boar->GetPerceivedPlayer();
	if (Player == nullptr)
		return EStateTreeRunStatus::Failed;

	// 射程内かつクールダウン終了済みなら、攻撃予兆へ移るため成功終了する。
	if (Boar->GetPerceivedPlayerDistance() <= Boar->GetPlayerAttackRange() && Boar->IsPlayerAttackReady())
		return EStateTreeRunStatus::Succeeded;

	// 対象が変わった場合や前回の移動要求が終了した場合だけ、NavMesh経路の移動要求を出し直す。
	const UPathFollowingComponent* PathFollowing = Controller->GetPathFollowingComponent();
	const bool bIsMoving = PathFollowing != nullptr && PathFollowing->GetStatus() != EPathFollowingStatus::Idle;
	if (InstanceData.ChaseTarget != Player || !bIsMoving)
	{
		const float AcceptanceRadius = Boar->GetPlayerAttackRange() * AcceptanceRangeRatio;
		Controller->MoveToActor(Player, AcceptanceRadius);
		InstanceData.ChaseTarget = Player;
	}

	return EStateTreeRunStatus::Running;
}
