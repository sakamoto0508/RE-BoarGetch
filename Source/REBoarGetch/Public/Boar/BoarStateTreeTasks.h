#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTasks.generated.h"

class AAIController;
class ACage;
class APawn;

USTRUCT()
struct FStateTreeBoarSenseInstanceData
{
	GENERATED_BODY()

	/// <summary> 実行中のイノシシPawnです。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;

	/// <summary> 実行中のAIControllerです。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/// <summary> 認識中プレイヤーです。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Output")
	TObjectPtr<APawn> TargetPlayer;

	/// <summary> 認識中の檻です。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Output")
	TObjectPtr<ACage> TargetCage;

	/// <summary> 視界内に有効対象があるかです。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Output")
	bool bHasTargetInSight = false;

	/// <summary> PlayerとCageのどちらを優先するかです。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Output")
	bool bPreferCage = false;

	/// <summary> 種別ロジックとして逃走を優先するかです。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Output")
	bool bPreferEscape = false;

	/// <summary> 檻攻撃可能種別かどうかです。 </summary>
	UPROPERTY(VisibleAnywhere, Category = "Output")
	bool bCanAttackCage = true;
};

USTRUCT(meta = (DisplayName = "Sense Player/Cage", Category = "Boar"))
struct FStateTreeBoarSenseTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FStateTreeBoarSenseTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FStateTreeBoarSenseInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/// <summary> Player/Cage認識を更新し、優先度出力を設定します。 </summary>
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
