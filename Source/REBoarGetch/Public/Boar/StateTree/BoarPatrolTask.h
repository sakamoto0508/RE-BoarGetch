#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarPatrolTask.generated.h"

USTRUCT(meta=(DisplayName="Boar Patrol", Category="Boar"))
struct FStateTreeBoarPatrolTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStateTreeInstanceData;
	
	/// <summary> 巡回ポイントの配列です。 </summary>
	UPROPERTY(EditAnywhere, Category = "Boar|Patrol")
	TArray<FVector> PatrolPoints;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}
	
	/// <summary> 巡回ポイントを順番に巡回するタスクです。 </summary>
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};