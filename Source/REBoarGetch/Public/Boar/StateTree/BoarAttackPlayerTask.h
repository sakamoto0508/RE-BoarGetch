#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "Boar/StateTree/BoarAttackTask.h"
#include "BoarAttackPlayerTask.generated.h"

class ABoarBase;
class APawn;

/** プレイヤー攻撃タスクが外部から受け取る入力データと進行状態です。 */
USTRUCT()
struct FBoarAttackPlayerTaskInstanceData
{
	GENERATED_BODY()

	/** 攻撃を実行するイノシシです。StateTreeのContextから自動で設定されます。 */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<APawn> NPC;

	/** 攻撃開始時にGlobal Senseの認識結果から取得したプレイヤーです。 */
	UPROPERTY()
	TObjectPtr<APawn> TargetPlayer;

	/**
	 * 現在の突進攻撃フェーズです。Telegraph → Charge → Retreat と進み、
	 * プレイヤーを認識し続けていればTelegraphへ戻り、見失えばRecoveryを経て終了します。
	 */
	EBoarChargeAttackPhase Phase = EBoarChargeAttackPhase::Telegraph;

	/** 現在フェーズの経過時間です。 */
	float PhaseElapsedTime = 0.0f;

	/** 予兆終了時に固定した直線方向です。 */
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
	bool bCleanupCompleted = false;
};

/** 予兆・直線突進・後退・硬直の順でプレイヤーへの攻撃を制御するStateTreeタスクです。 */
USTRUCT(meta=(DisplayName="Attack Player", Category="Boar"))
struct FStateTreeBoarAttackPlayerTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	/** 攻撃フェーズを毎フレーム進行するようタスクを初期化します。 */
	FStateTreeBoarAttackPlayerTask()
	{
		bShouldCallTick = true;
	}

	using FInstanceDataType = FBoarAttackPlayerTaskInstanceData;

	/** インスタンスデータの型を返します。 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/** 移動を止めて攻撃予兆を開始します。 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	/** 予兆・突進・後退・硬直を順に更新します。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	/** 捕獲・Stage停止などの中断時を含めて移動速度と接触記録を元へ戻します。 */
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

private:
	/** 突進を終了して後退へ移ります。 */
	void BeginRetreat(FInstanceDataType& InstanceData, ABoarBase* Boar) const;

	/** 攻撃予兆を開始します。タスク開始時と、後退後に再び突進する時に使います。 */
	void BeginTelegraph(FInstanceDataType& InstanceData, ABoarBase* Boar) const;

	/** 後退中に変更した自動回転の設定を元へ戻します。 */
	void RestoreRotationSettings(const FInstanceDataType& InstanceData, ABoarBase* Boar) const;

	/** プレイヤーを見失った後、硬直へ移ります。 */
	void BeginRecovery(FInstanceDataType& InstanceData, ABoarBase* Boar) const;

	/** タスク終了時の共通復元処理です。 */
	void FinishAttack(FInstanceDataType& InstanceData, ABoarBase* Boar) const;
};
