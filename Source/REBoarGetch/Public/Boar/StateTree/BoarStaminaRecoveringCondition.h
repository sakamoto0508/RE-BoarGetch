#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "BoarStaminaRecoveringCondition.generated.h"

class APawn;

/** スタミナ回復判定条件の入力データです。 */
USTRUCT()
struct FBoarStaminaRecoveringConditionInstanceData
{
	GENERATED_BODY()

	/** 判定するイノシシです。StateTreeのContextから自動で設定されます。 */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;
};

/**
 * スタミナ切れで停止回復中か判定するStateTree条件です。
 * 減速のみの設定やスタミナ非対応の種別では常にfalseとなり、通常ボアの遷移には影響しません。
 */
USTRUCT(meta=(DisplayName="Boar Stamina Recovering", Category="Boar"))
struct FStateTreeBoarStaminaRecoveringCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStaminaRecoveringConditionInstanceData;

	/** インスタンスデータの型を返します。 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/** 停止回復中か判定します。 */
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	/** 判定結果を反転するかです。 */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;
};
