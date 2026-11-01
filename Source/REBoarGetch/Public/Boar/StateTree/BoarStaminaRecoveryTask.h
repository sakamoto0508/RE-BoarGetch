#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStaminaRecoveryTask.generated.h"

class APawn;

/** スタミナ回復タスクの入力データです。 */
USTRUCT()
struct FBoarStaminaRecoveryTaskInstanceData
{
	GENERATED_BODY()

	/** 回復するイノシシです。StateTreeのContextから自動で設定されます。 */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

/**
 * スタミナ切れのイノシシをその場で停止させ、復帰割合まで回復したら成功終了するStateTreeタスクです。
 * スタミナの増減はABoarBase::UpdateStaminaが担当し、このタスクは移動要求を出さずに待つだけです。
 */
USTRUCT(meta=(DisplayName="Boar Stamina Recovery", Category="Boar"))
struct FStateTreeBoarStaminaRecoveryTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStaminaRecoveryTaskInstanceData;

	/** インスタンスデータの型を返します。 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/** 移動要求を止めて回復待機を開始します。回復済みの場合は即座に成功します。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	/** 回復完了で成功、捕獲で失敗を返します。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
};
