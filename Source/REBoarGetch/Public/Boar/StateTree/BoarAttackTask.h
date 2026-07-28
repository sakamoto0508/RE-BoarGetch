#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarAttackTask.generated.h"

class ACage;
class APawn;

/** 檻攻撃タスクが外部から受け取る入力データです。 */
USTRUCT()
struct FBoarAttackTaskInstanceData
{
	GENERATED_BODY()

	/** 攻撃を実行するイノシシです。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> NPC;

	/** Global Senseが認識している攻撃対象の檻です。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<ACage> TargetCage;
};

USTRUCT(meta=(DisplayName="Attack Cage", Category="Boar"))
struct FStateTreeBoarAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarAttackTaskInstanceData;

	/**
	 * インスタンスデータの型を返します。
	 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/**
	 * ケージを攻撃するタスク。
	 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
