#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarSenseTask.generated.h"

USTRUCT(meta=(DisplayName="Sense Player/Cage", Category="Boar"))
struct FStateTreeBoarSenseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FStateTreeBoarSenseTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoarStateTreeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}
	
	/** プレイヤー・檻の認識情報を更新し、次に優先して行動する対象（プレイヤーまたは檻）を決定します。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	/** ノードの説明を取得します。 */
	virtual FText GetDescription(const FGuid& ID,FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
