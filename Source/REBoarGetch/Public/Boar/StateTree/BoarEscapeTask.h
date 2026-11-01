#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.h"
#include "BoarEscapeTask.generated.h"

class APawn;

/** 逃走地点を計算するタスクの入力・出力データです。 */
USTRUCT()
struct FBoarEscapeTaskInstanceData
{
	GENERATED_BODY()

	/** 逃走するイノシシです。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> NPC;

	/** Global Senseが認識しているプレイヤーです。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> TargetPlayer;

	/** NavMesh上に決定した逃走先です。 */
	UPROPERTY(EditAnywhere, Category = "Output")
	FVector EscapeLocation = FVector::ZeroVector;

	/** 次に逃走先の再判定を行うまでの残り秒数です。 */
	UPROPERTY()
	float ReplanTimeRemaining = 0.0f;
};

/**
 * 認識したプレイヤーから離れる方向へ逃走地点を決定するStateTreeタスクです。
 * 真後ろが塞がっている場合は左右へ角度をずらした候補、さらにランダム地点の順に代替先を探します。
 * State中は逃走フラグを立て、DataAssetの逃走速度とスタミナ消費を有効にします。
 */
USTRUCT(meta=(DisplayName="Boar Escape", Category="Boar"))
struct FStateTreeBoarEscapeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarEscapeTaskInstanceData;

	/** 逃走中に逃走先がプレイヤー側へ向いていないか確認する間隔です。0以下で再判定しません。 */
	UPROPERTY(EditAnywhere, Category = "Boar|Escape", meta = (ClampMin = "0.0"))
	float ReplanInterval = 0.5f;

	/** 逃走先として採用する現在地からの最小距離(cm)です。近すぎる候補は即座に到着してしまうため除外します。 */
	UPROPERTY(EditAnywhere, Category = "Boar|Escape", meta = (ClampMin = "0.0"))
	float MinEscapeDistance = 300.0f;

	/** 最小距離を逃走距離(EscapeRange)の何割までに抑えるかです。実際の最小距離はMinEscapeDistanceとの小さい方です。 */
	UPROPERTY(EditAnywhere, Category = "Boar|Escape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinEscapeDistanceRatio = 0.5f;

	/**
	 * インスタンスデータの型を返します。
	 */
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	/**
	 * プレイヤーから逃げる方向を計算し、逃走地点を決定します。
	 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	/** スタミナ切れを検知して失敗終了し、プレイヤーが回り込んだ場合は逃走先を更新します。 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	/** 逃走フラグを解除し、通常速度へ戻します。 */
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
