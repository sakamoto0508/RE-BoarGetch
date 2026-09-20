// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Stage/StageRunData.h"
#include "BoarGameMode.generated.h"

/** 捕獲数が変化したとき、現在数と目標数をHUDへ通知します。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCapturedBoarCountChangedSignature,
	int32, CurrentCount,
	int32, TargetCount);

/**
 * ゲームのルール全体を管理します。
 * 現在収容数、挑戦中記録、フレーム終端の終了判定、保存と演出の順序を管理します。
 */
UCLASS()
class REBOARGETCH_API ABoarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** このゲームで使用するPlayerControllerとデフォルトPawnを設定します。 */
	ABoarGameMode();

	/** 捕獲成功時の共通後処理（檻送致/カウント/ドロップ）を実行します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Game")
	void HandleBoarCaptured(class ABoarBase* Boar);

	/** プレイヤー死亡時の終了処理を実行します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Game")
	void HandlePlayerDeath(class ABoarPlayerCharacter* PlayerCharacter);

	/** 現在の檻収容数を返します。累計捕獲数ではありません。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Game")
	int32 GetCapturedBoarCount() const { return CapturedBoarCount; }

	/** 現在のステージ設定を返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage")
	class UStageConfig* GetStageConfig() const { return StageConfig; }

	/** 捕獲数が変化したことをHUDなどへ通知します。 */
	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Capture")
	FOnCapturedBoarCountChangedSignature OnCapturedBoarCountChanged;

	/** 檻の収容・解放通知を受け、全檻の現在数を再集計します。 */
	void RefreshHousedBoarCount();
	/** Clear要求後のダメージ・解放を即時遮断します。GameOver要求だけでは同フレームClearを妨げません。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage")
	bool CanAdvanceStage() const { return StageState == EBoarStageState::Playing && !bStageClearRequested; }
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage")
	EBoarStageState GetStageState() const { return StageState; }
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage")
	FStageRunData GetStageRunData() const { return StageRunData; }
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage")
	void RequestStageClear();
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage")
	void RequestGameOver();
	/** 任意の開始演出を実装した場合、完了時に呼びます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage")
	void CompleteStageIntroduction();
	/** 演出を待つ設定時に呼びます。Clearは保存成功後だけResultへ進みます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage")
	void CompleteStageEndPresentation();
	/** 保存失敗から再試行する入口です。失敗時UIの見た目は固定しません。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage")
	bool RetryClearSave();
	bool RecordSpecialCoin(FName CoinId);

protected:
	/** Stage開始時にStageConfigのSpawn EntryからBoarを生成します。 */
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStageIntroductionRequested();

	/** ゲームオーバー演出・遷移をBP側で実装するためのイベントです。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Game")
	void OnGameOver();

	/** ステージクリア演出や画面遷移をBlueprint側で実装するためのイベントです。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStageCleared();

private:
	void HandleWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void ResolveStageEnd();
	void SetStageActorsStopped(bool bStopped);
	FDelegateHandle WorldTickEndHandle;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage", meta = (AllowPrivateAccess = "true"))
	EBoarStageState StageState = EBoarStageState::Preparing;
	UPROPERTY(Transient) FStageRunData StageRunData;
	/** 未確定の演出時間は固定せず、BPから完了通知を受けられます。未設定は従来どおり即開始します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bWaitForStageIntroduction = false;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bWaitForStageEndPresentation = false;
	bool bGameOverRequested = false;
	bool bClearSaveSucceeded = false;
	bool bEndPresentationCompleted = false;
	bool bEndUIShown = false;
	TSet<TWeakObjectPtr<class ABoarBase>> HandledCapturedBoars;
	/** Level内のBoarSpawnPointを使用して設定済みBoarを生成します。 */
	void SpawnConfiguredBoars();
	void SpawnConfiguredSpecialCoins();
	/** コインの見た目・Overlapを持つ既存BPを指定します。未設定では生成しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Spawn")
	TSubclassOf<class ASpecialCoin> SpecialCoinClass;

	/** 現在の捕獲数がステージクリア条件を満たしたか評価します。 */
	void EvaluateStageClearCondition();

	/** ステージ固有の設定です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UStageConfig> StageConfig;

	/** 捕獲時に回復ピックアップを出す確率です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Capture", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float HealItemDropChance = 0.3f;

	/** 捕獲時に生成する回復ピックアップのクラスです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Capture", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<class AHealPickup> HealPickupClass;

	/** 現在の檻収容数です。旧プロパティ名はBP互換のため保持します。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "REBoarGetch|Capture", meta = (AllowPrivateAccess = "true"))
	int32 CapturedBoarCount = 0;

	/** ステージクリア要求の多重発生を防ぎます。 */
	bool bStageClearRequested = false;
};
