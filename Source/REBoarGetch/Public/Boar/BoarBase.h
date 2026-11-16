// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Boar/BoarDataAsset.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "BoarBase.generated.h"

class UCaptureComponent;
class ACage;
class APawn;
class APatrolPath;
class UAnimSequenceBase;
class UAnimMontage;

/** 逃走開始の旋回と、逃走中の大きな切り返しの段階です（AKI-152）。AnimBPはこれを読んで見た目を切り替えます。 */
UENUM(BlueprintType)
enum class EBoarEscapeTurnPhase : uint8
{
	/** 通常の走行です。 */
	None,
	/** 逃走開始時、その場で素早く逃走方向へ向きを変えています。 */
	StartTurn,
	/** 切り返しのため減速しています。 */
	Decelerate,
	/** 停止して踏ん張っています。 */
	Brace,
	/** 踏ん張った姿勢のまま止まっています。 */
	Hold,
	/** 停止したままゆっくり旋回しています。 */
	Turn,
	/** 逃走開始・切り返しの後に加速しています。 */
	Accelerate,
};

UCLASS()
class REBOARGETCH_API ABoarBase : public ACharacter
{
	GENERATED_BODY()

public:
	/** 永続個体IDです。手置きはEditor、生成個体はStageConfigから設定します。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Boar|Identity")
	FName BoarUniqueId;
	/** 捕獲Componentを生成します。TickはBeginPlay後、スタミナを使う種別だけ有効になります。 */
	ABoarBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	/** GameModeからBeginPlay前に呼べる、個体設定の初期化です。 */
	void InitializeForStage();
	/** 種別設定と初期スタミナを適用します。 */
	virtual void BeginPlay() override;
	/** 種別がスタミナを使用する場合に消費・回復状態を更新します。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 捕獲Componentの状態を確定し、成功時だけGameModeへ捕獲後処理を通知します。 */
	UFUNCTION(BlueprintCallable, Category = "Boar")
	void Capture();
	/** Net passes one shared-feedback flag per swing; success state is still owned by CaptureComponent. */
	bool CaptureWithFeedback(bool bSharedFeedback);
	
	/** 捕獲済み個体を解放し、到達可能なNavMesh地点まで歩かせてから通常AIへ戻します。 */
	UFUNCTION(BlueprintCallable, Category = "Boar")
	void ReleaseBoar();

	/**
	 * 捕まっているかどうかを返します。
	 */
	UFUNCTION(BlueprintPure, Category = "Boar")
	bool IsCaptured() const;

	/**
	 * 周囲のプレイヤーと檻を同期探索し、距離・視野角・遮蔽条件を満たす最寄り対象を保存します。
	 * StateTree Sense Taskから反復されるため、呼び出し側が更新頻度を管理してください。
	 */
	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	bool RefreshPerceptionTargets();

	/** 現在認識中のプレイヤーです。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	APawn* GetPerceivedPlayer() const { return PerceivedPlayer; }

	/** 現在認識中の檻です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	ACage* GetPerceivedCage() const { return PerceivedCage; }

	/** 現在認識中プレイヤーまでの距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPerceivedPlayerDistance() const { return PerceivedPlayerDistance; }

	/** 現在認識中の檻までの距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPerceivedCageDistance() const { return PerceivedCageDistance; }

	/** 視認距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetSightRange() const { return ActiveSettings.SightRange; }

	/** 追跡目安距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetChaseRange() const { return ActiveSettings.ChaseRange; }

	/** 逃走目安距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetEscapeRange() const { return ActiveSettings.EscapeRange; }

	/** 檻優先度係数です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetCagePriorityWeight() const { return ActiveSettings.CagePriorityWeight; }

	/** プレイヤー優先度係数です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPlayerPriorityWeight() const { return ActiveSettings.PlayerPriorityWeight; }

	/** 逃走優先度係数です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetEscapePriorityWeight() const { return ActiveSettings.EscapePriorityWeight; }

	/** 檻攻撃可能か返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool CanAttackCage() const { return ActiveSettings.bCanAttackCage; }

	/** 現在の種類が檻へ与えるダメージです。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetCageAttackDamage() const { return ActiveSettings.CageAttackDamage; }

	/** 現在の予兆アニメーションまたは仮設定から攻撃予兆時間を返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI|Attack")
	float GetAttackTelegraphDuration() const;

	/** 攻撃予兆アニメーションの再生速度を返します。 */
	float GetAttackTelegraphPlayRate() const { return ActiveSettings.AttackTelegraphPlayRate; }

	/** 突進速度を返します。 */
	float GetChargeSpeed() const { return ActiveSettings.ChargeSpeed; }

	/** 突進の着弾距離を返します。 */
	float GetChargeImpactDistance() const { return ActiveSettings.ChargeImpactDistance; }

	/** 突進の最大継続時間を返します。 */
	float GetChargeMaxDuration() const { return ActiveSettings.ChargeMaxDuration; }

	/** 着弾後の後退距離を返します。 */
	float GetAttackRetreatDistance() const { return ActiveSettings.AttackRetreatDistance; }

	/** 着弾後の後退速度を返します。 */
	float GetAttackRetreatSpeed() const { return ActiveSettings.AttackRetreatSpeed; }

	/** プレイヤーへの攻撃予兆を開始する距離を返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI|Attack")
	float GetPlayerAttackRange() const { return ActiveSettings.PlayerAttackRange; }

	/** プレイヤーへの突進後の硬直時間を返します。 */
	float GetAttackRecoveryDuration() const { return ActiveSettings.AttackRecoveryDuration; }

	/** プレイヤーへの攻撃クールダウンが終わっているか返します。 */
	bool IsPlayerAttackReady() const;
	/** 認識・捕獲状態・射程・Cooldownを一箇所で評価し、攻撃開始の抜け道を作りません。 */
	bool CanStartPlayerAttack() const;

	/** プレイヤーへの攻撃終了時に呼び、次の攻撃までのクールダウンを開始します。 */
	void StartPlayerAttackCooldown();

	/** 突進中の接触記録を初期化し、NotifyHitからの記録を開始します。 */
	void BeginChargeHitTracking();

	/** 突進中の接触記録を停止します。 */
	void EndChargeHitTracking();

	/** 突進中に歩行不可面（プレイヤー・壁など）と接触したか返します。 */
	bool HasChargeBlockingHit() const { return bChargeBlockingHit; }

	/** 突進の接触記録に使用します。ダメージ処理はプレイヤー側の既存接触処理が担当します。 */
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse,
		const FHitResult& Hit) override;

	/** 攻撃予兆開始をアニメーションBlueprintへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Animation")
	void OnAttackTelegraphStarted(float Duration, float PlayRate);

	/** 突進開始をアニメーションBlueprintへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Animation")
	void OnChargeStarted();

	/** 突進攻撃終了をアニメーションBlueprintへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Animation")
	void OnChargeAttackFinished();

	/** 現在の種別設定を返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	EBoarArchetype GetBoarArchetype() const { return BoarArchetype; }

	/** 種別設定を切り替え、共通パラメータを再適用します。 */
	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	void SetBoarArchetype(EBoarArchetype NewArchetype);

	/**
	 * 現在の認識結果と種別から、プレイヤー・檻・逃走のどれを優先するかを判定します。
	 * Sense TaskとStateTree Conditionの両方から同じ判定を使うための共通処理です。
	 */
	void GetTargetPreference(bool& bOutPreferPlayer, bool& bOutPreferCage, bool& bOutPreferEscape) const;

	/** 種別ロジックとして逃走を優先するか返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool ShouldPreferEscape() const;

	/** 青イノシシのスタミナ比率(0-1)を返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetStaminaNormalized() const;

	/** 青イノシシがスタミナ回復待機中か返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool IsStaminaRecovering() const { return bIsRecoveringStamina; }

	/** スタミナ切れ中に移動を止める種別か返します。StateTreeの回復State判定に使用します。 */
	bool ShouldStopWhileRecoveringStamina() const { return ActiveSettings.bUseStamina && ActiveSettings.bStopWhileRecoveringStamina; }

	/** 逃走Stateで移動中か返します。アニメーションの走行判定にも使用できます。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool IsEscaping() const { return bIsEscaping; }

	/** StateTreeの逃走タスクから呼び、逃走速度とスタミナ消費対象を切り替えます。 */
	void SetEscaping(bool bNewEscaping);

	/** 現在の種別が逃走開始の旋回と大きな切り返しを使うか返します。 */
	bool IsEscapeTurnEnabled() const { return ActiveSettings.EscapeTurn.bEnabled; }

	/**
	 * 逃走Stateに入ったときに呼びます。直前まで逃走していた場合（継続）は切り返しを判定し、
	 * そうでなければ逃走開始の旋回と加速を始めます。切り返しを始めた場合はtrueを返します。
	 */
	bool BeginEscapeMotion(const FVector& EscapeDirection);

	/** 逃走中に逃走先を引き直してよいか返します（逃走開始・切り返し後の継続走行区間を過ぎ、旋回中でない）。 */
	bool CanReplanEscape() const;

	/** 向きとの差が切り返し角度以上なら切り返しを始めてtrueを返します。再発動間隔中はfalseです。 */
	bool NeedsEscapePivot(const FVector& EscapeDirection) const;
	/** bSkipDecelerate：到着直後の再入など、新しい経路が既に入っているときは減速を飛ばして踏ん張りから始めます。 */
	bool TryStartEscapePivot(const FVector& EscapeDirection, bool bSkipDecelerate = false);

	/** 減速を終えて停止中（踏ん張り・旋回）か返します。保留中の逃走先はこの間に経路へ反映します。 */
	bool IsEscapeMovementHeld() const;

	/** 逃走開始の旋回・切り返しの段階です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	EBoarEscapeTurnPhase GetEscapeTurnPhase() const { return EscapeTurnPhase; }

	/** 旋回方向です。+1で右（時計回り）、-1で左、旋回していないときは0です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	float GetEscapeTurnDirection() const;

	/** 切り返し中（減速〜旋回）か返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	bool IsEscapePivoting() const;

	/** 現在の旋回速度（度/秒）です。旋回していないときは0です。AnimBPで旋回ループの再生速度を合わせる用途に使います。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	float GetEscapeTurnRateDegreesPerSecond() const;

	/** 旋回・切り返しの状態を中断し、移動速度と向き追従を元に戻します。 */
	void ResetEscapeMotion();

	/**
	 * 旋回・切り返しの段階が変わったときの見た目の通知です。既定の実装は下のMontage設定を既存のDefaultSlotで再生します。
	 * Blueprintでoverrideすれば見た目を差し替えられます。Gameplay（移動・旋回・捕獲）はこの結果に依存しません。
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Boar|Animation")
	void OnEscapeTurnPhaseChanged(EBoarEscapeTurnPhase NewPhase, float TurnDirection);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	/** DataAssetから適用された通常時の移動速度を返します。アニメーションの速度正規化に使用します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	float GetBaseWalkSpeed() const { return ActiveSettings.BaseWalkSpeed; }

	/** 旧来の固定巡回ルートを返します。現在のランダムNavMesh巡回では未使用です。 */
	UFUNCTION(BlueprintPure)
	APatrolPath* GetPatrolPath() const { return PatrolPath; }

private:
	friend class FBoarGameplayRegressionTest;
	bool bSettingsInitialized = false;
	/** 距離・視野角・遮蔽・絶対発見距離から対象を認識できるか判定します。 */
	bool CanDetectTarget(const AActor* TargetActor, float Distance) const;

	/** 檻から離れる移動が完了したとき、通常のStateTree AIを再開します。 */
	UFUNCTION()
	void HandleReleaseMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);

	/** 解放移動の監視を終了し、通常のStateTree AIへ戻します。 */
	void FinishReleaseMovement();

	/** 種別ごとの共通AIパラメータを適用します。 */
	void ApplyArchetypeDefaults();

	/** 種別に応じたスタミナ更新を行います。 */
	void UpdateStamina(float DeltaSeconds);

	/** 逃走・スタミナ状態からMaxWalkSpeedを決定します。速度変更はこの関数に集約します。 */
	void ApplyMovementSpeed();

	/** スタミナ切れになった瞬間に、AIの移動要求と現在の慣性を止めます。 */
	void StopForStaminaRecovery();

	/** 旋回・切り返しの段階を進めます。 */
	void TickEscapeTurn(float DeltaSeconds);
	void SetEscapeTurnPhase(EBoarEscapeTurnPhase NewPhase);
	/** 固定した旋回方向へ目標の向きまで回します。到達したらtrueです。 */
	bool StepEscapeRotation(float RateDegreesPerSecond, float DeltaSeconds);
	/** 向き追従（bOrientRotationToMovement）を止める／元に戻します。 */
	void HoldOrientRotation();
	void RestoreOrientRotation();
	/** 継続走行区間を今から始めます。 */
	void StartEscapeCommit();
	/** 逃走中の速度倍率（減速・停止・加速）です。 */
	float GetEscapeTurnSpeedScale() const;
	/** スタミナか旋回のどちらかで毎フレーム更新が必要な間だけTickします。 */
	void UpdateTickEnabled();

	/** 切り返しの減速〜停止で1回再生し、最終フレームで保持するMontageです（自動Blend Outを無効にして使います）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapeBraceMontage;

	/** その場旋回のループMontageです（左右）。逃走開始の素早い旋回にも再生速度を上げて使います。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapeTurnLeftMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapeTurnRightMontage;

	/** 旋回ループを等速（再生速度1）で再生したときの旋回速度です。足滑りを抑えるため実際の旋回速度に再生速度を合わせます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "DegreesPerSecond"))
	float EscapeTurnAnimationRate = 150.0f;

	/** 旋回ループの再生速度の上限です（逃走開始の素早い旋回で使います）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float EscapeTurnMaxPlayRate = 2.0f;

	/** 旋回Montageを止めるときのBlend Out時間です。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "Seconds"))
	float EscapeTurnBlendOutTime = 0.12f;

	/** 現在の旋回・切り返しの段階です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	EBoarEscapeTurnPhase EscapeTurnPhase = EBoarEscapeTurnPhase::None;
	/** 現在の段階に入ってからの経過秒数です。 */
	float EscapeTurnPhaseElapsed = 0.0f;
	/** 旋回開始時に固定した目標Yawと回転方向（+1右 / -1左）です。旋回中に再抽選しません。 */
	float EscapeTurnTargetYaw = 0.0f;
	float EscapeTurnSign = 0.0f;
	/** 加速を始める速度割合と加速時間です（逃走開始と切り返しで異なる）。 */
	float EscapeAccelerationStartRatio = 1.0f;
	float EscapeAccelerationDuration = 0.0f;
	/** 向き追従を止める前の値です。 */
	bool bSavedOrientRotationToMovement = true;
	bool bOrientRotationHeld = false;
	/** 継続走行区間の開始時刻と開始位置です。 */
	double EscapeCommitStartTime = -1.0;
	FVector EscapeCommitStartLocation = FVector::ZeroVector;
	/** 前回の切り返し開始時刻と、前回逃走Stateを抜けた時刻です。 */
	double LastEscapePivotTime = -1.0e9;
	double LastEscapeExitTime = -1.0e9;

	/**
	 * 捕獲状態を管理するコンポーネントです。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/** 檻から解放するときにNavMesh上の出現地点を探す半径です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|Capture",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ReleaseRadius = 300.0f;

	/** 解放移動を完了と判定する目的地からの距離です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|Capture",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ReleaseAcceptanceRadius = 50.0f;

	/** 現在、檻から歩いて離れている途中かを表します。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|Capture",
		meta = (AllowPrivateAccess = "true"))
	bool bIsLeavingCage = false;

	/** イノシシ種別です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	EBoarArchetype BoarArchetype = EBoarArchetype::Normal;

	/** 種別ごとのAI・スタミナ・移動設定です。未設定時はコード上の既定値を使用します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoarDataAsset> BoarDataAsset;

	/** DataAssetから適用した現在の種別設定です。値の調整はBoarDataAsset側で行い、ここでは確認だけできます。 */
	UPROPERTY(VisibleInstanceOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	FBoarArchetypeSettings ActiveSettings;

	/** 次にプレイヤーへの攻撃を開始できるワールド時刻です。 */
	double NextPlayerAttackTime = 0.0;

	/** 突進中の接触を記録しているかです。 */
	bool bTrackingChargeHit = false;

	/** 突進中に歩行不可面と接触したかです。 */
	bool bChargeBlockingHit = false;

	/** 認識中のプレイヤーです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APawn> PerceivedPlayer;

	/** 最後に視認したプレイヤーです。ThreatMemoryRange内にいる間は視界外でも認識を継続します。 */
	TWeakObjectPtr<APawn> RememberedThreatPlayer;

	/** 認識中の檻です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ACage> PerceivedCage;

	/** プレイヤーとの距離です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	float PerceivedPlayerDistance = BIG_NUMBER;

	/** 檻との距離です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	float PerceivedCageDistance = BIG_NUMBER;

	/** 現在スタミナです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",meta = (AllowPrivateAccess = "true"))
	float CurrentStamina = 100.0f;

	/** スタミナ回復待機中かです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",meta = (AllowPrivateAccess = "true"))
	bool bIsRecoveringStamina = false;

	/** 逃走State中かです。StateTreeの逃走タスクが切り替えます。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	bool bIsEscaping = false;

	/** 固定巡回を再導入する場合に使う予約参照です。現在のStateTree Patrol Taskからは未使用です。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Boar|AI", meta=(AllowPrivateAccess="true"))
	TObjectPtr<APatrolPath> PatrolPath;
};
