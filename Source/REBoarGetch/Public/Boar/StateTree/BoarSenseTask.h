#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarSenseTask.generated.h"

/** プレイヤーと檻を認識し、次に優先する行動対象を共有データへ反映するStateTreeタスクです。 */
USTRUCT(meta=(DisplayName="Sense Player/Cage", Category="Boar"))
struct FStateTreeBoarSenseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStateTreeInstanceData;
	
	/** 毎フレーム認識情報を更新するようタスクを初期化します。 */
	FStateTreeBoarSenseTask()
	{
		bShouldCallTick = true;
		//bShouldStateChangeOnReselect = false;
	}

	/** このタスクが使用する共有インスタンスデータ型を返します。 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}
	
	/** プレイヤー・檻の認識情報を更新し、次に優先して行動する対象（プレイヤーまたは檻）を決定します。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	
	/** タスクの更新処理を行います。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,float DeltaTime) const override;
	
	/** プレイヤー・檻の認識情報を更新し、次に優先して行動する対象（プレイヤーまたは檻）を決定します。 */
	EStateTreeRunStatus UpdateSense(FStateTreeExecutionContext& Context) const;

#if WITH_EDITOR
	/** ノードの説明を取得します。 */
	virtual FText GetDescription(const FGuid& ID,FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
