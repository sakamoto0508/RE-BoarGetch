#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarAttackTask.generated.h"

class ACage;
class APawn;

/** 檻への突進攻撃を構成する進行フェーズです。 */
UENUM()
enum class EBoarChargeAttackPhase : uint8
{
	/** 攻撃前に停止し、プレイヤーへ突進を予告するフェーズです。 */
	Telegraph,
	/** 固定した方向へ高速移動し、檻との接触時にダメージを与えるフェーズです。 */
	Charge,
	/** 攻撃後に開始地点付近へ戻り、次の行動へ移れる状態にするフェーズです。 */
	Retreat,
	/** プレイヤーへの突進・後退後、その場で短く硬直するフェーズです。 */
	Recovery
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

	/** タスク開始前の、移動方向へ自動回転する設定です。 */
	bool bOriginalOrientRotationToMovement = false;

	/** タスク開始前の、Controllerの向きへ自動回転する設定です。 */
	bool bOriginalUseControllerDesiredRotation = false;

	/** 突進終了通知を送信済みかです。 */
	bool bChargeFinishedNotified = false;

	/** 終了処理の重複実行を防ぎます。 */
	bool bCleanupCompleted = true;
};

/** 予兆・直線突進・後退・硬直の順で檻への攻撃を制御するStateTreeタスクです。プレイヤー攻撃（Attack Player）と同じ流れで、後退中も檻の方を向き続けます。 */
USTRUCT(meta=(DisplayName="Attack Cage", Category="Boar"))
struct FStateTreeBoarAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	/** 攻撃フェーズを毎フレーム進行するようタスクを初期化します。 */
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

	/** 予兆・突進・後退・硬直を順に更新します。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	/** 中断時を含めて移動速度を元へ戻します。 */
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

private:
	/** 突進を終了して後退へ移ります。 */
	void BeginRetreat(FInstanceDataType& InstanceData, class ABoarBase* Boar) const;

	/** 後退中に変更した自動回転の設定を元へ戻します。 */
	void RestoreRotationSettings(const FInstanceDataType& InstanceData, class ABoarBase* Boar) const;

	/** タスク終了時の共通復元処理です。 */
	void FinishAttack(FInstanceDataType& InstanceData, class ABoarBase* Boar) const;
};
