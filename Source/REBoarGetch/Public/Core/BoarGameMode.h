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

	/**
	 * 捕獲演出の開始時に、捕獲状態になったBoarを「確定待ち」として登録します（演出完了まで檻収容・記録を保留）。
	 * 確定待ちの正本はこのGameModeです。演出側は見た目だけを持ち、完了・中断をここへ通知します。
	 */
	void RegisterPendingCapture(class ABoarBase* Boar);
	/**
	 * 確定待ちの捕獲を確定します（HandleBoarCapturedと同じ判定：Stage進行中・捕獲中・未確定のときだけ収容・記録）。
	 * 渡したBoarは確定できなかった場合も確定待ちから外します。収容数の再集計・通知は確定ごとに1回（1体も確定しなければ最後に1回）です。確定できたBoarを返します。
	 */
	TArray<class ABoarBase*> CompletePendingCaptures(const TArray<TWeakObjectPtr<class ABoarBase>>& Boars);
	/** 確定せずに確定待ちから外します（対象Destroy・Level破棄・見た目だけの片付け）。 */
	void DiscardPendingCapture(const TWeakObjectPtr<class ABoarBase>& Boar);
	bool IsCapturePending(const class ABoarBase* Boar) const;

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
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage")
	FText GetInitializationError() const { return InitializationError; }

protected:
	/** Stage開始時にStageConfigのSpawn EntryからBoarを生成します。 */
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStageIntroductionRequested();
	/** 独自演出の通知欠落時に呼びます。BPは自身のカメラ・Widget等を解除できます。進行再開はC++が保証します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStagePresentationTimedOut(bool bIntroduction);
	/** Native recovery UI is also available if the stage cannot be prepared. */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStageInitializationFailed(const FText& Reason);

	/** ゲームオーバー演出・遷移をBP側で実装するためのイベントです。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Game")
	void OnGameOver();

	/** ステージクリア演出や画面遷移をBlueprint側で実装するためのイベントです。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStageCleared();

private:
	friend class FBoarStageInitializationTest;
	friend class FBoarStageEndRegressionTest;
	friend class FBoarPendingCaptureTest;
	/** 捕獲を確定して収容・記録します。確定できた（今回初めて確定した）場合true。 */
	bool ConfirmCapture(class ABoarBase* Boar);
	/** Validate -> spawn -> initialize -> check readiness; BeginPlay follows this sequence. */
	bool PrepareStage();
	bool ValidateStageSetup();
	bool InitializeStageActors();
	bool FailInitialization(const FText& Reason);
	bool bStartupAttempted = false;
	UPROPERTY(Transient) FText InitializationError;
	bool bStageInitializationComplete = false;
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
	/** 独自BP演出の最大待機時間です。完了通知が欠落しても進行を再開します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation", meta = (ClampMin = "0.1", Units = "s"))
	float StageIntroductionTimeoutSeconds = 30.f;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation", meta = (ClampMin = "0.1", Units = "s"))
	float StageEndPresentationTimeoutSeconds = 30.f;
	FTimerHandle IntroductionTimeoutHandle;
	FTimerHandle EndPresentationTimeoutHandle;
	void HandleIntroductionTimeout();
	void HandleEndPresentationTimeout();
	/** Clear時にPlayerControllerのStageClearPresentation（勝利カメラ・消失・暗転）を再生してからResultへ進みます。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bUseStageClearPresentation = true;
	/** GameOver時にPlayerControllerのGameOverPresentation（ダウン・カメラ後退・警告UI）を再生してからGameOver UIへ進みます。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bUseGameOverPresentation = true;
	bool bGameOverRequested = false;
	bool bClearSaveSucceeded = false;
	bool bEndPresentationCompleted = false;
	bool bEndUIShown = false;
	/** 確定済みの捕獲（二重確定防止）。解放されたBoarは再集計時に外れます。 */
	TSet<TWeakObjectPtr<class ABoarBase>> HandledCapturedBoars;
	/** 捕獲状態だが演出完了まで確定を保留しているBoar。HandledCapturedBoarsと同時には入りません。 */
	TSet<TWeakObjectPtr<class ABoarBase>> PendingCapturedBoars;
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
