#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateDebugTask.generated.h"

class APawn;

/** StateTreeのステート開始・完了・終了を確認するための入力データです。 */
USTRUCT()
struct FBoarStateDebugTaskInstanceData
{
	GENERATED_BODY()

	/** デバッグ対象のイノシシです。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> NPC;

	/** StateTreeエディタ上で設定するステート名です。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName StateName = NAME_None;
};

/** StateTreeの実際の遷移を画面とOutput Logへ表示するデバッグタスクです。 */
USTRUCT(meta = (DisplayName = "Boar State Debug", Category = "Boar|Debug"))
struct FStateTreeBoarStateDebugTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStateDebugTaskInstanceData;

	FStateTreeBoarStateDebugTask()
	{
		bShouldCallTick = false;
#if WITH_EDITORONLY_DATA
		bConsideredForCompletion = false;
		bCanEditConsideredForCompletion = false;
#endif
	}

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual void StateCompleted(
		FStateTreeExecutionContext& Context,
		EStateTreeRunStatus CompletionStatus,
		const FStateTreeActiveStates& CompletedActiveStates) const override;
};
