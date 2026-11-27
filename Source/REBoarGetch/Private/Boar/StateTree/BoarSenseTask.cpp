#include "Boar/StateTree/BoarSenseTask.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"

#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"

// プレイヤー・檻の認識情報を更新し、
// 次に優先して行動する対象（プレイヤーまたは檻）を決定する。
EStateTreeRunStatus FStateTreeBoarSenseTask::EnterState(FStateTreeExecutionContext& Context
	, const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	return UpdateSense(Context);
}

EStateTreeRunStatus FStateTreeBoarSenseTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	(void)DeltaTime;
	return UpdateSense(Context);
}

EStateTreeRunStatus FStateTreeBoarSenseTask::UpdateSense(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// このStateTreeを実行しているイノシシを取得する。
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());

	// イノシシを取得できなかった場合は認識情報を初期化して失敗終了する。
	if (Boar == nullptr)
	{
		InstanceData.TargetPlayer = nullptr;
		InstanceData.TargetCage = nullptr;
		InstanceData.bHasTargetInSight = false;
		InstanceData.bPreferCage = false;
		InstanceData.bPreferPlayer = false;
		InstanceData.bPreferEscape = false;
		InstanceData.bCanAttackCage = false;
		InstanceData.bIsStaminaRecovering = false;
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bHasTargetInSight = Boar->RefreshPerceptionTargets();

	// StateTreeのTransition Conditionはこの出力だけを読むため、認識結果を毎Tick写しておく。
	InstanceData.TargetPlayer = Boar->GetPerceivedPlayer();
	InstanceData.TargetCage = Boar->GetPerceivedCage();
	InstanceData.bHasTargetPlayer = InstanceData.TargetPlayer != nullptr;
	InstanceData.bHasTargetCage = InstanceData.TargetCage != nullptr;
	InstanceData.DistanceToCage = Boar->GetPerceivedCageDistance();
	InstanceData.bCanAttackCage = Boar->CanAttackCage();

	// 檻を攻撃しない種類では、檻を認識していても反応する対象に数えない。
	const bool bHasPlayer = InstanceData.TargetPlayer != nullptr;
	const bool bHasCage = InstanceData.TargetCage != nullptr && InstanceData.bCanAttackCage;

	// 種類ごとの仕様に従い、発見した対象への反応を決定する。
	Boar->GetTargetPreference(InstanceData.bPreferPlayer, InstanceData.bPreferCage, InstanceData.bPreferEscape);

	InstanceData.DistanceToPlayer = Boar->GetPerceivedPlayerDistance();

	// 回復中も認識は更新し続け、回復State側の遷移条件に停止状態を渡す。
	InstanceData.bIsStaminaRecovering = Boar->ShouldStopWhileRecoveringStamina() && Boar->IsStaminaRecovering();

	if (!bHasPlayer && !bHasCage)
	{
		InstanceData.bPreferCage = false;

		// 対象がいないことは正常な徘徊状態なので、失敗として扱わない。
		return EStateTreeRunStatus::Running;
	}

	// 認識結果は出力値とTransition Conditionで分岐するため、更新できた時点で成功とする。
	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FStateTreeBoarSenseTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
                                              const IStateTreeBindingLookup& BindingLookup,
                                              EStateTreeNodeFormatting Formatting) const
{
	(void)ID;
	(void)InstanceDataView;
	(void)BindingLookup;
	(void)Formatting;
	return FText::FromString(TEXT("<b>Sense Player/Cage</b>"));
}
#endif
