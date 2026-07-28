#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarEscapeTask.generated.h"

class APawn;

/** 逃走地点を計算するタスクの入力・出力データです。 */
USTRUCT()
struct FBoarEscapeTaskInstanceData
{
	GENERATED_BODY()

	/** 逃走するイノシシです。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> NPC;

	/** Global Senseが認識しているプレイヤーです。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> TargetPlayer;

	/** NavMesh上に決定した逃走先です。 */
	UPROPERTY(EditAnywhere, Category = "Output")
	FVector EscapeLocation = FVector::ZeroVector;
};

USTRUCT(meta=(DisplayName="Boar Escape", Category="Boar"))
struct FStateTreeBoarEscapeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarEscapeTaskInstanceData;

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
