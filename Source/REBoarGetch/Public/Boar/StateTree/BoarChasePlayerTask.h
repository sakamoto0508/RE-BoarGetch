#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarChasePlayerTask.generated.h"

class APawn;

/** プレイヤー追跡タスクの入力データと進行状態です。 */
USTRUCT()
struct FBoarChasePlayerTaskInstanceData
{
	GENERATED_BODY()

	/** 追跡するイノシシです。StateTreeのContextから自動で設定されます。 */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;

	/** 現在移動要求を出している追跡対象です。 */
	UPROPERTY()
	TObjectPtr<APawn> ChaseTarget;
};

/**
 * Global Senseが認識しているプレイヤーをNavMesh経路で追跡するStateTreeタスクです。
 * 攻撃射程に入りクールダウンが終わっていればSucceeded、プレイヤーを見失うとFailedを返します。
 */
USTRUCT(meta=(DisplayName="Chase Player", Category="Boar"))
struct FStateTreeBoarChasePlayerTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	/** 追跡対象と射程を毎フレーム確認するようタスクを初期化します。 */
	FStateTreeBoarChasePlayerTask()
	{
		bShouldCallTick = true;
	}

	using FInstanceDataType = FBoarChasePlayerTaskInstanceData;

	/** インスタンスデータの型を返します。 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/** 認識中のプレイヤーへの移動を開始します。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	/** 射程・見失いを判定し、必要なら移動要求を更新します。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	/** 追跡移動を停止します。 */
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	/** 攻撃射程に対して、どこまで近づいてから停止するかの割合です。 */
	UPROPERTY(EditAnywhere, Category = "Boar|Chase", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AcceptanceRangeRatio = 0.5f;

private:
	/** 現在の状態から追跡を継続・成功・失敗のどれにするか判定し、移動要求を更新します。 */
	EStateTreeRunStatus UpdateChase(FInstanceDataType& InstanceData) const;
};
