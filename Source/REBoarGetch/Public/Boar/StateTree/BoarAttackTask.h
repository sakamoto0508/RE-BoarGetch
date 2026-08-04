#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarAttackTask.generated.h"

class ACage;
class APawn;

UENUM()
enum class EBoarChargeAttackPhase : uint8
{
	Telegraph,
	Charge,
	Retreat
};

/** 檻攻撃タスクが外部から受け取る入力データです。 */
USTRUCT()
struct FBoarAttackTaskInstanceData
{
	GENERATED_BODY()

	/** 攻撃を実行するイノシシです。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> NPC;

	/** Global Senseが認識している攻撃対象の檻です。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<ACage> TargetCage;

	/** 現在の突進攻撃フェーズです。 */
	EBoarChargeAttackPhase Phase = EBoarChargeAttackPhase::Telegraph;

	/** 現在フェーズの経過時間です。 */
	float PhaseElapsedTime = 0.0f;

	/** 突進開始時に固定した直線方向です。 */
	FVector ChargeDirection = FVector::ForwardVector;

	/** 後退開始位置です。 */
	FVector RetreatStartLocation = FVector::ZeroVector;

	/** タスク開始前の移動速度です。 */
	float OriginalWalkSpeed = 0.0f;

	/** 終了処理の重複実行を防ぎます。 */
	bool bCleanupCompleted = false;
};

USTRUCT(meta=(DisplayName="Attack Cage", Category="Boar"))
struct FStateTreeBoarAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FStateTreeBoarAttackTask()
	{
		bShouldCallTick = true;
	}

	using FInstanceDataType = FBoarAttackTaskInstanceData;

	/**
	 * インスタンスデータの型を返します。
	 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/**
	 * ケージを攻撃するタスク。
	 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	/** 予兆・突進・後退を順に更新します。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	/** 中断時を含めて移動速度を元へ戻します。 */
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

private:
	/** タスク終了時の共通復元処理です。 */
	void FinishAttack(FInstanceDataType& InstanceData, class ABoarBase* Boar) const;
};
