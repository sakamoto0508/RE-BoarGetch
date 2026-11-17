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

/** 切り返し（BrakeAndPivot）を始めた理由です。処理は共通で、デバッグと調整の区別にだけ使います（AKI-152／AKI-172）。 */
UENUM(BlueprintType)
enum class EBoarEscapePivotReason : uint8
{
	None,
	/** 方向維持時間（N秒）の終わりの再評価で、プレイヤーから離れる方向が維持方向と大きく違う（回り込まれた）。 */
	Reevaluate,
	/** 進路の先が障害物・NavMesh端で、到達可能な別方向へ大きく向きを変える。 */
	NaturalObstacle,
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

	/** 新しい逃走を始めるときに呼びます。逃走開始の旋回（必要なら）と加速を始め、方向維持は走り出してから始まります。 */
	void BeginEscapeMotion(const FVector& EscapeDirection);

	/**
	 * 逃走Stateを別Stateへの遷移で抜けるときに呼びます（AKI-172）。速度・回転・Montageは通常の逃走終了と同じく元に戻し、
	 * 逃走方向・方向維持の経過秒数・旋回の段階だけを控えて、同じ逃走へすぐ戻ったときに再開できるようにします。
	 * StateTreeの停止（StopLogic）・捕獲・EndPlayでは呼ばず、SetEscaping(false)で完全に終えます。
	 */
	void SuspendEscape();

	/**
	 * 逃走中（EscapeTurn有効の種別）は、逃げ切るまで逃げている相手を認識し続けます（AKI-172）。
	 * 最短EscapeMinDuration秒、かつ見失ってからEscapeLoseDuration秒は、視界外・記憶範囲外でもPerceivedPlayerに残します。
	 * RefreshPerceptionTargetsの中で、通常の認識の後に呼びます。
	 */
	void UpdateEscapeThreatHold();

	/**
	 * 逃走Stateに再Enter（Changed）したとき、控えた逃走を再開できれば再開してtrueを返します（AKI-172）。
	 * 再開の条件：別Stateへの遷移で抜けた（停止・捕獲・EndPlayで終えていない）、同じプレイヤーから逃げている、
	 * 抜けてからContinueEscapeGraceDuration以内。再開時はプレイヤーの位置で方向を計算し直さず、
	 * 逃走方向と方向維持の残り時間をそのまま引き継ぎます（別State中の時間は数えません）。
	 * 旋回の途中で抜けていた場合は、固定済みの旋回先へ同じ段階の旋回を続けます。
	 */
	bool TryResumeEscape(const AActor* Threat);

	/**
	 * 維持している逃走方向（逃走の大方向）です。逃走開始・切り返し・障害物復帰・N秒ごとの再評価のときだけ更新し、
	 * NavMesh経路の小さな曲がりやActorの向きの揺れでは変えません。切り返し角度の比較基準にも使います（AKI-152）。
	 */
	bool HasEscapeHeading() const { return bHasEscapeHeading; }
	FVector GetEscapeHeading() const { return EscapeHeading; }
	/** 維持方向を変えます。方向維持時間は変えません（新しい方向を確定するときはCommitEscapeHeadingを使います）。 */
	void SetEscapeHeading(const FVector& Direction);
	/**
	 * 新しい逃走方向を確定し、方向維持時間（DirectionCommitDuration）をここから始め直します（AKI-172）。
	 * 障害物復帰・再評価で止まらずに方向を変えたときに使います。切り返し・逃走開始では再加速の開始時に自動で始まります。
	 */
	void CommitEscapeHeading(const FVector& Direction, const TCHAR* DebugReason);
	/** 切り返しの判定角度（度）です。基準は維持方向、無ければActorの正面です。 */
	float GetEscapeReferenceYawDelta(const FVector& Direction) const;

	/** 逃げている相手（認識中のプレイヤー）を設定します。Escape TaskのTickから渡します（距離のデバッグ表示用）。 */
	void SetEscapeThreat(const AActor* Threat) { if (Threat != nullptr) EscapeThreat = Threat; }

	/** 方向維持の経過秒数と、その間の移動距離（cm）です。始まっていないときは負の値です。 */
	float GetEscapeCommitElapsed() const;
	float GetEscapeCommitDistance() const;
	/** 方向維持の残り秒数です。始まっていない（旋回中など）ときは負の値です。 */
	float GetEscapeCommitRemaining() const;
	/** 方向維持時間（N秒）を過ぎ、方向を再評価する時期か返します。旋回中・方向維持が始まっていないときはfalseです。 */
	bool IsEscapeCommitExpired() const;

	/** 維持方向との差が切り返し角度以上か返します。障害物（NaturalObstacle）ではObstaclePivotAngleThresholdを使います。 */
	bool NeedsEscapePivot(const FVector& EscapeDirection, EBoarEscapePivotReason Reason = EBoarEscapePivotReason::Reevaluate) const;
	/**
	 * 切り返し（減速→踏ん張り→停止旋回→加速）を始めます。角度不足・旋回中・連続切り返しの抑制中（再評価以外）はfalseです。
	 * bSkipDecelerate：到着直後の再入など、新しい経路が既に入っているときは減速を飛ばして踏ん張りから始めます。
	 */
	bool TryStartEscapePivot(const FVector& EscapeDirection, bool bSkipDecelerate = false,
		EBoarEscapePivotReason Reason = EBoarEscapePivotReason::Reevaluate);

	/** 直近の切り返しの理由です（AnimBP・デバッグ用）。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	EBoarEscapePivotReason GetEscapePivotReason() const { return LastEscapePivotReason; }

	/** 連続切り返しの抑制中か返します（前回の切り返し・逃走開始旋回を終えてからMinPivotInterval未満）。 */
	bool IsEscapePivotCoolingDown() const;

	/** 逃げている相手（プレイヤー）までの水平距離です。不明なら負の値です（デバッグ用）。 */
	float GetEscapeThreatDistance() const;

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

	/**
	 * 同じ逃走が続いたままEscape Stateが再選択されたとき（逃走先に到着→Rootから再選択）に呼びます。
	 * 逃走開始の旋回はやり直しません。旋回・切り返しの途中ならそのまま続け（減速中に到着した場合は踏ん張りへ進める）、trueを返します。
	 * 旋回していなければ何もせずfalseを返します。次の逃走先と切り返しの判断はEscape Taskの共通判定が行います。
	 */
	bool ContinueEscapeMotion();

	/** 切り返しを始められない理由です。始められるときはnullptrです（デバッグログ用）。 */
	const TCHAR* GetEscapePivotRejectReason(const FVector& EscapeDirection,
		EBoarEscapePivotReason Reason = EBoarEscapePivotReason::Reevaluate) const;

	/** 逃走中の実効速度の上限（MaxWalkSpeed、cm/s）です。デバッグログ用。 */
	float GetEscapeEffectiveMaxSpeed() const;

	/** 直進区間・区間終了評価の設定値（DataAssetのEscapeTurn）です。 */
	const FBoarEscapeTurnSettings& GetEscapeTurnSettings() const { return ActiveSettings.EscapeTurn; }

	/** 現在の向きと方向の差（度、-180〜180）です。 */
	float GetEscapeYawDelta(const FVector& Direction) const;

	/** コンソール変数 Boar.AI.EscapeTurnDebug が有効か返します。 */
	static bool IsEscapeTurnDebugEnabled();

	/** 逃走・切り返しのデバッグログを出します。呼び出し側でIsEscapeTurnDebugEnabled()を確認してから文字列を作ってください。 */
	void LogEscapeTurn(const FString& Message) const;

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

	/**
	 * アニメーションの速度正規化に使う基準速度を返します（ABP_NormalBoarが速度÷この値でBlendSpaceを選びます）。
	 * DataAssetのAnimationReferenceSpeedが設定されていればそれを、無ければ通常時の移動速度（BaseWalkSpeed）を返します。
	 * 関数名はABPのノード参照を保つため従来のままです。移動速度そのものはC++側でActiveSettings.BaseWalkSpeedを使います。
	 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation", meta = (DisplayName = "Get Animation Reference Speed"))
	float GetBaseWalkSpeed() const
	{
		return ActiveSettings.AnimationReferenceSpeed > 0.0f ? ActiveSettings.AnimationReferenceSpeed : ActiveSettings.BaseWalkSpeed;
	}

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
	/** 方向維持（N秒）を今から始めます。bFromTurnEnd：逃走開始旋回・切り返しを終えて走り出した（連続切り返し抑制の基準にもする）。 */
	void StartEscapeCommit(bool bFromTurnEnd);
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
	/** 方向維持（N秒）の開始時刻と開始位置です。旋回中は-1です。 */
	double EscapeCommitStartTime = -1.0;
	FVector EscapeCommitStartLocation = FVector::ZeroVector;
	/** 前回の切り返し開始時刻と、前回逃走Stateを抜けた時刻です。 */
	double LastEscapePivotTime = -1.0e9;
	double LastEscapeExitTime = -1.0e9;
	/** 今の逃走を始めた時刻と、逃走相手を見失った時刻です（逃げ切り判定。負の値は未設定）。 */
	double EscapeSessionStartTime = -1.0;
	double EscapeThreatLostTime = -1.0;
	/**
	 * SuspendEscapeで控えた逃走です（AKI-172）。再開できるか（bEscapeSuspended）、逃走方向、方向維持の経過秒数（旋回中は負）、
	 * 抜けたときの旋回段階です。ResetEscapeMotion（捕獲・EndPlay・逃走終了）で破棄します。
	 */
	bool bEscapeSuspended = false;
	FVector SuspendedEscapeHeading = FVector::ForwardVector;
	float SuspendedEscapeCommitElapsed = -1.0f;
	EBoarEscapeTurnPhase SuspendedEscapeTurnPhase = EBoarEscapeTurnPhase::None;
	/** 前回の切り返し・逃走開始旋回を終えて再加速を始めた時刻です。連続切り返しの抑制（MinPivotInterval）の基準です。 */
	double LastEscapePivotEndTime = -1.0e9;
	/** 直近の切り返しの理由です。 */
	EBoarEscapePivotReason LastEscapePivotReason = EBoarEscapePivotReason::None;
	/** 今回の切り返しの停止旋回速度です（PivotMinTurnDurationで角度に応じて落とす）。 */
	float EscapePivotTurnRate = 0.0f;
	/** 逃げている相手です（デバッグの距離表示用）。見失っても直前の相手を保持します。 */
	TWeakObjectPtr<const AActor> EscapeThreat;
	/** デバッグ：今回の切り返しの開始時刻、開始時と最接近時のプレイヤー距離です（捕獲チャンスが届く距離だったかの検証用）。 */
	double EscapePivotStartTime = -1.0;
	float EscapePivotStartThreatDistance = -1.0f;
	float EscapePivotMinThreatDistance = -1.0f;
	/** デバッグ：今回の切り返しで「止まってから」の経過秒数の計測開始時刻（踏ん張り開始）です。 */
	double EscapePivotHeldStartTime = -1.0;
	/** 逃走中の向き追従の旋回速度を、通常走行用（RunTurnRate）と通常歩行用（TurnRate）で切り替えます。 */
	void ApplyEscapeRotationRate();
	/** 維持している逃走方向（水平の単位ベクトル）です。ResetEscapeMotionで消します。 */
	FVector EscapeHeading = FVector::ForwardVector;
	bool bHasEscapeHeading = false;
	/** デバッグ用：逃走開始の旋回と切り返しの発動回数です。 */
	int32 EscapeStartTurnCount = 0;
	int32 EscapePivotCount = 0;

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
