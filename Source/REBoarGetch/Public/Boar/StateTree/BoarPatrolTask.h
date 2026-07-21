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
	
	/** 現在地を中心に、NavMesh上の徘徊地点を探す半径です。 */
	UPROPERTY(EditAnywhere, Category = "Boar|Patrol", meta = (ClampMin = "0.0"))
	float PatrolRadius = 1000.0f;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}
	
	/** NavMesh上から到達可能な徘徊地点をランダムに選択するタスクです。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
