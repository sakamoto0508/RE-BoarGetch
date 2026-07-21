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
	// StateTreeが保持しているインスタンスデータを取得する。
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
		return EStateTreeRunStatus::Failed;
	}
	
	// Perceptionを更新し、現在認識している対象を取得する。
	InstanceData.bHasTargetInSight = Boar->RefreshPerceptionTargets();
	// 認識しているプレイヤーを保存する。
	InstanceData.TargetPlayer = Boar->GetPerceivedPlayer();
	// 認識している檻を保存する。
	InstanceData.TargetCage = Boar->GetPerceivedCage();
	// 認識しているプレイヤーが存在するかを保存する。
	InstanceData.bHasTargetPlayer = InstanceData.TargetPlayer != nullptr;
	// 認識している檻が存在するかを保存する。
	InstanceData.bHasTargetCage = InstanceData.TargetCage != nullptr;
	// 認識している檻までの距離を保存する。
	InstanceData.DistanceToCage = Boar->GetPerceivedCageDistance();
	// 現在檻を攻撃できる状態か更新する。
	InstanceData.bCanAttackCage = Boar->CanAttackCage();
	// プレイヤー・檻を認識しているか判定する。
	const bool bHasPlayer = InstanceData.TargetPlayer != nullptr;
	const bool bHasCage = InstanceData.TargetCage != nullptr && InstanceData.bCanAttackCage;
	InstanceData.bPreferPlayer = false;
	InstanceData.bPreferCage = false;
	InstanceData.bPreferEscape = false;

	// 種類ごとの仕様に従い、発見した対象への反応を決定する。
	switch (Boar->GetBoarArchetype())
	{
	case EBoarArchetype::Normal:
		InstanceData.bPreferEscape = bHasPlayer;
		InstanceData.bPreferCage = !bHasPlayer && bHasCage;
		break;
	case EBoarArchetype::Red:
		InstanceData.bPreferPlayer = bHasPlayer;
		InstanceData.bPreferCage = !bHasPlayer && bHasCage;
		break;
	case EBoarArchetype::Blue:
		InstanceData.bPreferEscape = bHasPlayer;
		break;
	case EBoarArchetype::CageBreaker:
		InstanceData.bPreferCage = bHasCage;
		InstanceData.bPreferPlayer = !bHasCage && bHasPlayer;
		break;
	case EBoarArchetype::EscapeSpecialist:
		InstanceData.bPreferEscape = bHasPlayer;
		InstanceData.bPreferCage = !bHasPlayer && bHasCage;
		break;
	default:
		break;
	}

	// プレイヤーも檻も見つかっていない場合。
	if (!bHasPlayer && !bHasCage)
	{
		// 優先対象は存在しない。
		InstanceData.bPreferCage = false;
		Boar->PrintAIStateDebug(TEXT("Patrol"));

		// 対象がいないことは正常な徘徊状態なので、失敗として扱わない。
		return EStateTreeRunStatus::Succeeded;
	}

	if (InstanceData.bPreferEscape)
	{
		Boar->PrintAIStateDebug(TEXT("Escape"));
	}
	else if (InstanceData.bPreferPlayer)
	{
		Boar->PrintAIStateDebug(TEXT("ChasePlayer"),
			InstanceData.TargetPlayer->GetActorLocation());
	}
	else if (InstanceData.bPreferCage)
	{
		Boar->PrintAIStateDebug(TEXT("ChaseCage"),
			InstanceData.TargetCage->GetActorLocation());
	}

	// 認識結果は出力値とTransition Conditionで分岐するため、更新できた時点で成功とする。
	return EStateTreeRunStatus::Succeeded;
}

// StateTreeエディタ上に表示するノード名を返す。
#if WITH_EDITOR
FText FStateTreeBoarSenseTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	(void)ID;
	(void)InstanceDataView;
	(void)BindingLookup;
	(void)Formatting;
	return FText::FromString(TEXT("<b>Sense Player/Cage</b>"));
}
#endif

