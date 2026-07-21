#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarAttackTask.generated.h"

USTRUCT(meta=(DisplayName="Attack Cage", Category="Boar"))
struct FStateTreeBoarAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStateTreeInstanceData;

	/**
	 * 攻撃力。
	 */
	UPROPERTY(EditAnywhere, Category="Attack")
	float Damage = 10.f;

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
