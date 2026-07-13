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
	// 逃走を優先する状態か更新する。
	InstanceData.bPreferEscape = Boar->ShouldPreferEscape();

	// プレイヤー・檻を認識しているか判定する。
	const bool bHasPlayer = InstanceData.TargetPlayer != nullptr;
	const bool bHasCage = InstanceData.TargetCage != nullptr && InstanceData.bCanAttackCage;

	// プレイヤーも檻も見つかっていない場合。
	if (!bHasPlayer && !bHasCage)
	{
		// 優先対象は存在しない。
		InstanceData.bPreferCage = false;

		// 逃走状態ならTask成功、それ以外は失敗。
		return InstanceData.bPreferEscape
			? EStateTreeRunStatus::Succeeded
			: EStateTreeRunStatus::Failed;
	}

	// プレイヤーまでの距離を優先度係数で補正したスコアを計算する。
	// 優先度係数が大きいほどプレイヤーを選びやすくなる。
	const float PlayerScore = bHasPlayer
		? Boar->GetPerceivedPlayerDistance() /
			FMath::Max(Boar->GetPlayerPriorityWeight(), KINDA_SMALL_NUMBER)
		: BIG_NUMBER;

	// 檻までの距離を優先度係数で補正したスコアを計算する。
	// 優先度係数が大きいほど檻を選びやすくなる。
	const float CageScore = bHasCage
		? Boar->GetPerceivedCageDistance() /
			FMath::Max(Boar->GetCagePriorityWeight(), KINDA_SMALL_NUMBER)
		: BIG_NUMBER;

	// スコアが小さい対象を優先ターゲットとして選択する。
	InstanceData.bPreferCage = CageScore < PlayerScore;

	// 認識対象が存在する、または逃走状態ならTask成功。
	return (InstanceData.bHasTargetInSight || InstanceData.bPreferEscape)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Failed;
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

