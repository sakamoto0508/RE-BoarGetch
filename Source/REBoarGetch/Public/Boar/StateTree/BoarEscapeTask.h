#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarEscapeTask.generated.h"

USTRUCT(meta=(DisplayName="Boar Escape", Category="Boar"))
struct FStateTreeBoarEscapeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStateTreeInstanceData;

	/**
	 * インスタンスデータの型を返します。
	 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/**
	 * プレイヤーから逃げる方向を計算し、逃走地点を決定します。
	 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
