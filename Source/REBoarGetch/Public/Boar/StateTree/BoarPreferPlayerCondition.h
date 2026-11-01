#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "BoarPreferPlayerCondition.generated.h"

class APawn;

/** プレイヤー優先判定条件の入力データです。 */
USTRUCT()
struct FBoarPreferPlayerConditionInstanceData
{
	GENERATED_BODY()

	/** 判定するイノシシです。StateTreeのContextから自動で設定されます。 */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

/**
 * Global Senseの認識結果に対して、種別がプレイヤーの追跡・攻撃を優先するか判定するStateTree条件です。
 * Sense Taskの bPreferPlayer と同じ ABoarBase::GetTargetPreference を使用します。
 */
USTRUCT(meta=(DisplayName="Boar Prefers Player", Category="Boar"))
struct FStateTreeBoarPreferPlayerCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarPreferPlayerConditionInstanceData;

	/** インスタンスデータの型を返します。 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/** プレイヤー優先状態か判定します。 */
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	/** 判定結果を反転するかです。 */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;
};
