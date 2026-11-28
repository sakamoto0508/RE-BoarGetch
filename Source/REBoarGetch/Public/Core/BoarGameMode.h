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

// ゲームのルール全体を管理します。
// 現在収容数、挑戦中記録、フレーム終端の終了判定、保存と演出の順序を管理します。
/** ステージの開始、捕獲確定、勝敗判定、結果の保存を管理する。 */
UCLASS()
class REBOARGETCH_API ABoarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** このゲームで使用するPlayerControllerとデフォルトPawnを設定します。 */
	ABoarGameMode();

	/** 捕獲を確定し、収容と報酬を処理する。bStartsNewCaptureUseがfalseの場合は直前の捕獲と同じ使用順を記録する。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Game")
	void HandleBoarCaptured(class ABoarBase* Boar, bool bStartsNewCaptureUse = true);

	/**
	 * 捕獲演出の開始時に、捕獲状態になったBoarを「確定待ち」として登録します（演出完了まで檻収容・記録を保留）。
	 * 確定待ちの正本はこのGameModeです。演出側は見た目だけを持ち、完了・中断をここへ通知します。
	 */
	void RegisterPendingCapture(class ABoarBase* Boar, bool bStartsNewCaptureUse = true);
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
	/**
	 * WorldContextのWorldでStageが進行中（CanAdvanceStage）かを返します。ABoarGameModeがないWorld（Lobby・施設・
	 * テスト用World）では、進行を止める理由がないためtrueです。各Actor・ComponentはGameModeを個別に探さずこれを使います。
	 */
	static bool IsStageProgressAllowed(const UObject* WorldContextObject);

	/**
	 * 捕獲状態になったBoarを、捕獲演出を再生できれば確定待ちに、できなければ即時確定にします。
	 * Gameplay（Boar）が演出を直接探さないための窓口です。ABoarGameModeが無いWorld（施設の練習用Boarなど）でも演出は再生し、
	 * その場合は確定（収容・記録）を行いません。BoarはCaptureComponentで捕獲状態になってから呼んでください。
	 */
	static void BeginCaptureResolution(class ABoarBase* Boar, bool bStartsNewCaptureUse);
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
	/**
	 * 演出を待つ設定時に呼びます。Clearは保存に失敗してもResultへ進み、Result上で警告と再試行を出します（S-1、2026-11-27決定）。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage")
	void CompleteStageEndPresentation();
	/** Clear時の保存を再試行します。Result表示後に成功した場合は、Resultの警告を消すよう通知します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage")
	bool RetryClearSave();
	bool RecordSpecialCoin(FName CoinId);
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage")
	FText GetInitializationError() const { return InitializationError; }

protected:
	/** ActorのBeginPlay前に必要なステージ依存を初期化する。 */
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStageIntroductionRequested();
	/** 独自演出の通知欠落時に呼びます。BPは自身のカメラ・Widget等を解除できます。進行再開はC++が保証します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Stage")
	void OnStagePresentationTimedOut(bool bIntroduction);
	/** ステージの準備に失敗した際、失敗理由をBlueprintへ通知する。 */
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
	/** 捕獲成功の順序をRunへ記録します。同じGadget使用（同じ一振り）の捕獲は、フレームが違っても同じ番号になります。 */
	void RecordCaptureOrder(const class ABoarBase* Boar, bool bStartsNewCaptureUse);
	/** Clear確定時に、全Cageの現在収容個体をRunへ確定します（1回だけ）。 */
	void SnapshotHousedBoars();
	/** 今回のRunの捕獲使用の通し番号（Run内だけで有効。保存しない）。 */
	int32 CaptureEventOrder = 0;
	/** 検証 → Spawn → 初期化 → 準備完了の確認。BeginPlayはこの後に続く。 */
	bool PrepareStage();
	bool ValidateStageSetup();
	bool InitializeStageActors();
	bool FailInitialization(const FText& Reason);
	/** ステージ初期化の実行を試みたか。 */
	bool bStartupAttempted = false;

	/** 初期化失敗時に復帰画面へ渡す理由。 */
	UPROPERTY(Transient)
	FText InitializationError;

	/** ステージ開始に必要な初期化を完了したか。 */
	bool bStageInitializationComplete = false;
	void HandleWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void ResolveStageEnd();
	void SetStageActorsStopped(bool bStopped);
	/** フレーム終端の終了判定の購読を解除するハンドル。 */
	FDelegateHandle WorldTickEndHandle;
	/** 現在のステージ進行状態。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage", meta = (AllowPrivateAccess = "true"))
	EBoarStageState StageState = EBoarStageState::Preparing;

	/** 今回の挑戦の時間・捕獲・取得結果の正本。 */
	UPROPERTY(Transient)
	FStageRunData StageRunData;

	/** 未確定の演出時間は固定せず、BPから完了通知を受けられます。未設定は従来どおり即開始します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bWaitForStageIntroduction = false;
	/** 終了UIを表示する前に演出完了通知を待つか。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bWaitForStageEndPresentation = false;
	/** 独自BP演出の最大待機時間です。完了通知が欠落しても進行を再開します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation", meta = (ClampMin = "0.1", Units = "s"))
	float StageIntroductionTimeoutSeconds = 30.f;
	/** 終了演出から通知が届かない場合の待機上限（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation", meta = (ClampMin = "0.1", Units = "s"))
	float StageEndPresentationTimeoutSeconds = 30.f;
	/** 開始演出の待機を取り消すためのタイマー。 */
	FTimerHandle IntroductionTimeoutHandle;
	/** 終了演出の待機を取り消すためのタイマー。 */
	FTimerHandle EndPresentationTimeoutHandle;
	void HandleIntroductionTimeout();
	void HandleEndPresentationTimeout();
	/** Clear時にPlayerControllerのStageClearPresentation（勝利カメラ・消失・暗転）を再生してからResultへ進みます。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bUseStageClearPresentation = true;
	/** GameOver時にPlayerControllerのGameOverPresentation（ダウン・カメラ後退・警告UI）を再生してからGameOver UIへ進みます。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Presentation")
	bool bUseGameOverPresentation = true;
	/** フレーム終端の判定にGameOver要求が届いているか。 */
	bool bGameOverRequested = false;
	/** クリア時の進行データの保存が成功したか。 */
	bool bClearSaveSucceeded = false;
	/** 終了演出の完了を受理済みか。 */
	bool bEndPresentationCompleted = false;
	/** 終了結果の画面を表示済みか。 */
	bool bEndUIShown = false;
	/** 確定済みの捕獲（二重確定防止）。解放されたBoarは再集計時に外れます。 */
	TSet<TWeakObjectPtr<class ABoarBase>> HandledCapturedBoars;
	/**
	 * 捕獲状態だが演出完了まで確定を保留しているBoarと、確定の期限（ゲーム時間・秒）。HandledCapturedBoarsと同時には入りません。
	 * 期限を過ぎても演出から完了通知が来なければ、GameModeが確定します（演出の不具合で捕獲を失わないため）。
	 */
	TMap<TWeakObjectPtr<class ABoarBase>, double> PendingCapturedBoars;
	/**
	 * 確定待ちの上限時間（ゲーム時間・秒）。Pause中は進みません。捕獲演出が確定を通知する時間（Hit Stop＋Slow＋Dissolve＋
	 * Transfer Finish、現在約0.85秒）より十分長くしてください。短いと演出の途中で確定し、Cageへの受信演出が出ません。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Capture", meta = (ClampMin = "0.5", Units = "s"))
	float PendingCaptureTimeoutSeconds = 3.f;
	/** 期限を過ぎた確定待ちを確定します。NowはWorldのゲーム時間（秒）。 */
	void ConfirmExpiredPendingCaptures(double Now);
	/** WorldのPlayerControllerが持つ捕獲演出Component。無ければnullptr。 */
	static class UCapturePresentationComponent* FindCapturePresentation(const UWorld* World);
	/** コインの見た目・Overlapを持つ既存BPを指定します。未設定では生成しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Stage|Spawn")
	TSubclassOf<class ASpecialCoin> SpecialCoinClass;

	/** 現在の捕獲数がステージクリア条件を満たしたか評価します。 */
	void EvaluateStageClearCondition();

	/** ステージ固有の設定です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UStageConfig> StageConfig;

	/** 捕獲時に回復ピックアップを出す確率（0～1）。 */
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
