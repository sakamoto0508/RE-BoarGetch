// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "GameFramework/Actor.h"
#include "Cage.generated.h"

class ABoarBase;
class UBoxComponent;
class USceneComponent;
class UCageVisualComponent;

/** 
 * * 捕獲したイノシシを収容する檻を管理するActorです。 
 *   主に以下の処理を担当します。 
 *  ・捕獲されたイノシシの収容 
 *  ・収容されたイノシシの配置位置計算 
 *  ・檻のHP管理 
 *  ・檻の破壊と再出現
 *  ・檻の破壊時に収容中のイノシシを解放 
 *  ・HP変更、破壊、再出現イベントの通知 * 
 *  檻は破壊時にActor自体をDestroyせず、
 *  一時的に非表示・Collision無効の状態にします。
 *  これにより、同じActorから一定時間後の再出現処理を実行できます。 
 * 
 */
UCLASS()
class REBOARGETCH_API ACage : public AActor
{
	GENERATED_BODY()

protected:
	/** HPを初期化し、初回のHP通知を行います。 */
	virtual void BeginPlay() override;
	/** レベル終了時に再出現Timerを解除し、破棄後のCallback実行を防ぎます。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** 収容範囲など、檻を構成するComponentの初期状態を構築します。 */
	ACage();
	/** GameModeがBoar生成前に一度だけ実行するStage初期化です。 */
	void InitializeForStage();
	/** 無被弾時間と自然回復だけをゲーム時間で更新します。 */
	virtual void Tick(float DeltaSeconds) override;
	/** 終了演出中の復活Timerと自然回復を停止します。 */
	void SetStageStopped(bool bStopped);
	/** 外部解放・Actor破棄時にも収容数を正しく減らします。 */
	void ForgetBoar(ABoarBase* Boar);

	/** 檻のHP変更を通知するデリゲートです。 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCageHealthChanged, float, CurrentHp, float, MaxHp);

	/** 檻の破壊を通知するデリゲートです。 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCageDestroyed);

	/** 檻の再出現を通知するデリゲートです。 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCageRespawned);

	/** 檻のHPが変更されたときに呼ばれるイベントです。 */
	UPROPERTY(BlueprintAssignable, Category = "Cage|Events")
	FOnCageHealthChanged OnHealthChanged;

	/** 檻が破壊されたときに呼ばれるイベントです。 */
	UPROPERTY(BlueprintAssignable, Category = "Cage|Events")
	FOnCageDestroyed OnCageDestroyed;

	/** 檻が再出現したときに呼ばれるイベントです。 */
	UPROPERTY(BlueprintAssignable, Category = "Cage|Events")
	FOnCageRespawned OnRespawned;

	/** 捕獲したイノシシを檻へ収容します。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void CollectBoar(ABoarBase* Boar);

	/** 収容中のイノシシ数を返します。 */
	UFUNCTION(BlueprintPure, Category = "Cage")
	int32 GetCapturedBoarCount() const;

	/** 檻の現在HPを返します。 */
	UFUNCTION(BlueprintPure, Category = "Cage")
	float GetHP() const { return CurrentHp; }
	float GetMaxHP() const { return MaxHp; }

	/** 檻へダメージを与えます。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void ApplyDamage(float Damage);

	/** 檻が破壊状態かどうかを返します。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	bool GetIsCageDestroyed() const { return bIsDestroyed; }

private:
	UPROPERTY(VisibleAnywhere,Category="Cage|Visual") TObjectPtr<UCageVisualComponent> Presentation;
	bool bStageInitialized = false;
	void NotifyHousedCountChanged();
	UFUNCTION() void HandleHousedBoarDestroyed(AActor* Actor);
	bool CanAdvance() const;
	bool bStageStopped = false;
	float SecondsSinceDamage = 0.0f;
	/** 回復量は未調整です。0で無効、Editorでステージごとに設定します。 */
	UPROPERTY(EditAnywhere, Category = "Cage|Recovery", meta = (ClampMin = "0.0"))
	float RecoveryPerSecond = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Cage|Recovery", meta = (ClampMin = "0.0"))
	float RecoveryDelay = 0.0f;
	/** 指定された収容枠に対応するイノシシの配置位置を返します。 */
	FVector GetCapturedBoarLocation(int32 SlotIndex, const ABoarBase* Boar) const;

	/** 檻のコンポーネントを接続するルートです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cage|Components",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 捕獲したイノシシを配置する範囲です。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cage|Components",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> CapturedBoarArea;

	/** 収容中のイノシシです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<ABoarBase>> CapturedBoars;

	/** 檻中央を基準にした収容位置のXYオフセットです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Capture Layout",
		meta = (AllowPrivateAccess = "true"))
	FVector2D CapturedBoarCenterOffset = FVector2D::ZeroVector;

	/** 収容したイノシシ同士の配置間隔です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Capture Layout",
		meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float CapturedBoarSpacing = 140.0f;

	/** 収容位置を一列に並べる最大数です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Capture Layout",
		meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 CapturedBoarsPerRow = 3;

	/** 檻の床面から追加する高さです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage|Capture Layout",
		meta = (AllowPrivateAccess = "true"))
	float CapturedBoarFloorOffset = 2.0f;

	/** 檻を破壊状態にします。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void DestroyCage();

	/** 檻を再出現させます。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void RespawnCage();

	/** 檻の現在HPです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	float CurrentHp = 100.f;

	/** 檻の最大HPです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage",meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MaxHp = 100.f;

	/** 檻が破壊されてから再出現するまでの時間です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float RespawnDelay = 10.f;

	/** 檻の再出現処理に使用するタイマーハンドルです。 */
	FTimerHandle RespawnTimerHandle;

	/** 破壊処理の多重実行を防ぐためのフラグです。 */
	bool bIsDestroyed = false;
};
