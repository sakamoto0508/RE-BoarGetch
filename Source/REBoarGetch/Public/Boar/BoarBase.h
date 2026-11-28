#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Boar/BoarDataAsset.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "BoarBase.generated.h"

class UCaptureComponent;
class UFootstepDustComponent;
class ACage;
class APawn;
class UAnimSequenceBase;
class UAnimMontage;
class UAudioComponent;
class USoundBase;

/** 逃走開始の旋回と、逃走中の大きな切り返しの段階です。AnimBPはこれを読んで見た目を切り替えます。 */
UENUM(BlueprintType)
enum class EBoarEscapeTurnPhase : uint8
{
	/** 通常の走行です。 */
	None,
	/** 逃走開始時、その場で素早く逃走方向へ向きを変えています。 */
	StartTurn,
	/** 切り返しのため減速しています。 */
	Decelerate,
	/** 停止して踏ん張りながら、切り返し先へ向きを変えています。 */
	Brace,
	/** 踏ん張った姿勢で止まったまま、切り返し先への向き変えを終えます（終わったら加速へ）。 */
	Hold,
	/** 逃走開始・切り返しの後に加速しています。 */
	Accelerate,
};

/** 切り返し（BrakeAndPivot）を始めた理由です。処理は共通で、デバッグと調整の区別にだけ使います。 */
UENUM(BlueprintType)
enum class EBoarEscapePivotReason : uint8
{
	None,
	/** 方向維持時間（N秒）の終わりの再評価で、プレイヤーから離れる方向が維持方向と大きく違う（回り込まれた）。 */
	Reevaluate,
	/** 進路の先が障害物・NavMesh端で、到達可能な別方向へ大きく向きを変える。 */
	NaturalObstacle,
};

/** SuspendEscapeで控えた逃走です。同じ相手の逃走へすぐ戻ったとき（TryResumeEscape）に再開するために使います。 */
struct FBoarSuspendedEscape
{
	/** 再開できる控えがあるか。falseなら他の値は使いません。 */
	bool bValid = false;
	/** 控えた時点の維持方向（水平の単位ベクトル）。 */
	FVector Heading = FVector::ForwardVector;
	/** 控えた時点の方向維持の経過秒数です。旋回中に控えたときは負の値です。 */
	float CommitElapsed = -1.0f;
	/** 控えた時点の旋回・切り返しの段階。 */
	EBoarEscapeTurnPhase Phase = EBoarEscapeTurnPhase::None;
};

/**
 * ABoarBaseの逃走モーションの状態です（逃走開始の旋回・大きな切り返し・方向維持・逃げ切り判定）。
 * 判断（いつ・どちらへ逃げるか）はEscape Taskが持ち、ここには段階の進行と移動・向きの制御に必要な値だけを置きます。
 * ResetEscapeMotion（逃走の終了・捕獲・解放）で戻すのは段階・方向・控えなど一部だけです。前回の時刻（LastExitTime・
 * LastPivotEndTime）、逃走の開始時刻・相手、旋回先は、終了直後の再入・再開の判定に使うため残します。
 */
USTRUCT()
struct FBoarEscapeMotionState
{
	GENERATED_BODY()

	// ---- 旋回・切り返しの段階 ----
	/** 現在の旋回・切り返しの段階です。 */
	UPROPERTY(VisibleInstanceOnly, Category = "EscapeTurn")
	EBoarEscapeTurnPhase Phase = EBoarEscapeTurnPhase::None;
	/** 直前の段階です（加速の開始が切り返しの後か、逃走開始の旋回の後かを見た目側で区別するため）。 */
	EBoarEscapeTurnPhase PreviousPhase = EBoarEscapeTurnPhase::None;
	/** 現在の段階に入ってからの経過秒数です。 */
	float PhaseElapsed = 0.0f;
	/** 旋回開始時に固定した目標Yaw（度）です。旋回中に再抽選しません。 */
	float TurnTargetYaw = 0.0f;
	/** 旋回の向き（+1右 / -1左、旋回していないときは0）です。 */
	float TurnSign = 0.0f;
	/** 加速を始める速度割合（0〜1）です（逃走開始と切り返しで異なる）。 */
	float AccelerationStartRatio = 1.0f;
	/** 目標速度まで加速する時間（秒）。 */
	float AccelerationDuration = 0.0f;
	/** 今回の切り返しの旋回速度（度/秒）です。踏ん張り（Brace）の開始時に、残り角度をBrace＋Holdの時間で回り切れる値に決めます。 */
	float PivotTurnRate = 0.0f;

	// ---- 向き追従 ----
	/** 向き追従（bOrientRotationToMovement）を止める前の値です。 */
	bool bSavedOrientRotationToMovement = true;
	/** 旋回中に向き追従を止めているか。 */
	bool bOrientRotationHeld = false;

	// ---- 維持方向と方向維持（N秒） ----
	/** 維持している逃走方向（水平の単位ベクトル）です。ResetEscapeMotionで無効にします。 */
	FVector Heading = FVector::ForwardVector;
	/** Headingが設定済みか。 */
	bool bHasHeading = false;
	/** 方向維持の開始時刻（ゲーム時間・秒）です。旋回中は-1です。 */
	double CommitStartTime = -1.0;
	/** 方向維持を始めたワールド座標。 */
	FVector CommitStartLocation = FVector::ZeroVector;

	// ---- 前回の時刻（連続切り返しの抑制・再入の判定） ----
	/** 前回の切り返しを始めた時刻（ゲーム時間・秒）。 */
	double LastPivotTime = -1.0e9;
	/** 前回の切り返し・逃走開始旋回を終えて再加速を始めた時刻です。連続切り返しの抑制（MinPivotInterval）の基準です。 */
	double LastPivotEndTime = -1.0e9;
	/** 前回、逃走Stateを抜けた時刻（ゲーム時間・秒）。 */
	double LastExitTime = -1.0e9;
	/** 直近の切り返しの理由です。 */
	EBoarEscapePivotReason LastPivotReason = EBoarEscapePivotReason::None;

	// ---- 逃走の継続（逃げ切り判定） ----
	/** 今回の逃走を始めた時刻（ゲーム時間・秒）。逃げ切って終えたら負の値です。 */
	double SessionStartTime = -1.0;
	/** 逃走相手を見失った時刻（ゲーム時間・秒）。見えている間は負の値です。 */
	double ThreatLostTime = -1.0;
	/** 逃げている相手です。見失っても直前の相手を保持します（寿命は延ばさない）。 */
	TWeakObjectPtr<const AActor> Threat;
	/** 別Stateへの遷移で控えた逃走です。 */
	FBoarSuspendedEscape Suspended;

	// ---- デバッグ計測（捕獲チャンスが届く距離だったかの検証用） ----
	/** 今回の切り返しの開始時刻（ゲーム時間・秒）。 */
	double PivotStartTime = -1.0;
	/** 切り返し開始時の逃走相手との水平距離（cm）。 */
	float PivotStartThreatDistance = -1.0f;
	/** 切り返し中の逃走相手との最短水平距離（cm）。 */
	float PivotMinThreatDistance = -1.0f;
	/** 切り返しで止まった（踏ん張りを始めた）時刻（ゲーム時間・秒）。 */
	double PivotHeldStartTime = -1.0;
	/** 逃走開始の旋回の発動回数。 */
	int32 StartTurnCount = 0;
	/** 切り返しの発動回数。 */
	int32 PivotCount = 0;
};

/** 種別設定に従って知覚・行動・スタミナ・捕獲状態を管理する。 */
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
	/** 網は1振りにつき1回だけ共有演出フラグを渡す。捕獲の成否はCaptureComponentが持つ。 */
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

	/**
	 * プレイヤーへの攻撃（突進）が当たる状態かを返します。予兆・後退・硬直・逃走・徘徊中の接触はダメージになりません。
	 * プレイヤー側の接触処理（HandleBoarContact）がダメージ・スタンを与えるかの判定に使います。
	 */
	bool IsPlayerAttackHitActive() const { return bTrackingChargeHit; }

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
	 * 逃走Stateを別Stateへの遷移で抜けるときに呼びます。速度・回転・Montageは通常の逃走終了と同じく元に戻し、
	 * 逃走方向・方向維持の経過秒数・旋回の段階だけを控えて、同じ逃走へすぐ戻ったときに再開できるようにします。
	 * StateTreeの停止（StopLogic）・捕獲・EndPlayでは呼ばず、SetEscaping(false)で完全に終えます。
	 */
	void SuspendEscape();

	/**
	 * 逃走中（EscapeTurn有効の種別）は、逃げ切るまで逃げている相手を認識し続けます。
	 * 最短EscapeMinDuration秒、かつ見失ってからEscapeLoseDuration秒は、視界外・記憶範囲外でもPerceivedPlayerに残します。
	 * RefreshPerceptionTargetsの中で、通常の認識の後に呼びます。
	 */
	void UpdateEscapeThreatHold();

	/**
	 * 逃走Stateに再Enter（Changed）したとき、控えた逃走を再開できれば再開してtrueを返します。
	 * 再開の条件：別Stateへの遷移で抜けた（停止・捕獲・EndPlayで終えていない）、同じプレイヤーから逃げている、
	 * 抜けてからContinueEscapeGraceDuration以内。再開時はプレイヤーの位置で方向を計算し直さず、
	 * 逃走方向と方向維持の残り時間をそのまま引き継ぎます（別State中の時間は数えません）。
	 * 旋回の途中で抜けていた場合は、固定済みの旋回先へ同じ段階の旋回を続けます。
	 */
	bool TryResumeEscape(const AActor* Threat);

	/**
	 * 維持している逃走方向（逃走の大方向）です。逃走開始・切り返し・障害物復帰・N秒ごとの再評価のときだけ更新し、
	 * NavMesh経路の小さな曲がりやActorの向きの揺れでは変えません。切り返し角度の比較基準にも使います。
	 */
	bool HasEscapeHeading() const { return Escape.bHasHeading; }
	FVector GetEscapeHeading() const { return Escape.Heading; }
	/** 維持方向を変えます。方向維持時間は変えません（新しい方向を確定するときはCommitEscapeHeadingを使います）。 */
	void SetEscapeHeading(const FVector& Direction);
	/**
	 * 新しい逃走方向を確定し、方向維持時間（DirectionCommitDuration）をここから始め直します。
	 * 障害物復帰・再評価で止まらずに方向を変えたときに使います。切り返し・逃走開始では再加速の開始時に自動で始まります。
	 */
	void CommitEscapeHeading(const FVector& Direction, const TCHAR* DebugReason);
	/** 切り返しの判定角度（度）です。基準は維持方向、無ければActorの正面です。 */
	float GetEscapeReferenceYawDelta(const FVector& Direction) const;

	/** 逃げている相手（認識中のプレイヤー）を設定します。Escape TaskのTickから渡します（距離のデバッグ表示用）。 */
	void SetEscapeThreat(const AActor* Threat) { if (Threat != nullptr) Escape.Threat = Threat; }

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
	 * 切り返し（減速→踏ん張りながら旋回→停止→加速）を始めます。角度不足・旋回中・連続切り返しの抑制中（再評価以外）はfalseです。
	 * bSkipDecelerate：到着直後の再入など、新しい経路が既に入っているときは減速を飛ばして踏ん張りから始めます。
	 */
	bool TryStartEscapePivot(const FVector& EscapeDirection, bool bSkipDecelerate = false,
		EBoarEscapePivotReason Reason = EBoarEscapePivotReason::Reevaluate);

	/** 直近の切り返しの理由です（AnimBP・デバッグ用）。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	EBoarEscapePivotReason GetEscapePivotReason() const { return Escape.LastPivotReason; }

	/** 連続切り返しの抑制中か返します（前回の切り返し・逃走開始旋回を終えてからMinPivotInterval未満）。 */
	bool IsEscapePivotCoolingDown() const;

	/** 逃げている相手（プレイヤー）までの水平距離です。不明なら負の値です（デバッグ用）。 */
	float GetEscapeThreatDistance() const;

	/** 減速を終えて停止中（踏ん張り・旋回）か返します。保留中の逃走先はこの間に経路へ反映します。 */
	bool IsEscapeMovementHeld() const;

	/** 逃走開始の旋回・切り返しの段階です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	EBoarEscapeTurnPhase GetEscapeTurnPhase() const { return Escape.Phase; }

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

	/** アニメ診断ログ（Boar.AI.AnimDebug）のTimerを、CVarが有効なら張り、無効なら止めます。BeginPlayとCVarの変更時に呼ばれます。 */
	void RefreshAnimDebugTimer();

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
	/** 種別のSE設定です（DataAssetのAudio）。足音はAnimNotify_BoarFootstepから参照します。 */
	const FBoarArchetypeAudioSettings& GetAudioSettings() const { return ActiveSettings.Audio; }

	/** Boarに追従する一回きりのSEを鳴らします。捕獲済み・Stage停止中・未設定なら鳴らしません。 */
	void PlayBoarSound(USoundBase* Sound, float VolumeMultiplier = 1.0f, bool bAs2D = false);

	/** 攻撃予兆（突進前のため・檻攻撃の予兆）開始の音です。StateTree TaskがOnAttackTelegraphStartedと同時に呼びます。 */
	void PlayAttackTelegraphSound();

	/** 疲労ループなど、このBoarが鳴らしている継続音を止めます（捕獲・Stage停止・EndPlay）。 */
	void StopBoarLoopingSounds();


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

private:
	friend class FBoarGameplayRegressionTest;
	/** 種別設定を初期化済みか。 */
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

	/** 逃走開始の素早い旋回で再生するループMontageです（左右）。切り返しでは使いません（踏ん張りのMontageの間に旋回します）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapeTurnLeftMontage;

	/** 逃走開始時に右へ旋回するMontage。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapeTurnRightMontage;

	// 踏み替えの足滑りを抑えるため、設定の基準旋回速度と実際の旋回速度の比で再生速度を調整する。
	// 左右いずれも未設定の場合は、踏ん張りの終端ポーズを保ったまま旋回する。
	/** 切り返し中に左へ踏み替えるループMontage。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapePivotSwingLeftMontage;

	/** 逃走中に右へ踏み替えるMontage。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapePivotSwingRightMontage;

	/** 切り返しの旋回を終えて再加速を始めるときに1回再生する、後脚で地面を蹴るMontageです（自動Blend OutでLocomotionへ戻る）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> EscapePushOffMontage;

	/** 旋回ループを等速（再生速度1）で再生したときの旋回速度です。足滑りを抑えるため実際の旋回速度に再生速度を合わせます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "DegreesPerSecond"))
	float EscapeTurnAnimationRate = 150.0f;

	/** 旋回ループの再生速度の上限です（逃走開始の素早い旋回で使います）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float EscapeTurnMaxPlayRate = 2.0f;

	/** 旋回Montageを止めるときのBlend Out時間です。 */
	UPROPERTY(EditDefaultsOnly, Category = "Boar|Animation|EscapeTurn", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "Seconds"))
	float EscapeTurnBlendOutTime = 0.12f;

	/** 逃走モーション（旋回・切り返し・方向維持・控えた逃走・デバッグ計測）の状態。逃走の状態はすべてここに置きます。 */
	UPROPERTY(VisibleInstanceOnly, Category = "Boar|AI|EscapeTurn", meta = (AllowPrivateAccess = "true"))
	FBoarEscapeMotionState Escape;

	/** アニメ診断ログ（Boar.AI.AnimDebug）を0.2秒ごとに出すTimerです。CVarが有効な間だけ張ります。 */
	FTimerHandle AnimDebugTimerHandle;
	/** Timerから呼ばれ、アニメ診断ログを1行出します。 */
	void LogAnimDebug() const;
	/** アニメ診断ログを1行出します（Boar.AI.AnimDebug有効時のみ）。Triggerは出力のきっかけ（tick／phase）です。 */
	void LogAnimDebugEvent(const TCHAR* Trigger) const;
	/** 逃走中の向き追従の旋回速度を、通常走行用（RunTurnRate）と通常歩行用（TurnRate）で切り替えます。 */
	void ApplyEscapeRotationRate();

	/** 捕獲状態を管理するコンポーネントです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/** 走行中の接地で足元に出す土煙（演出のみ）。既定ではBlue Boar（EscapeSpecialist）の速い移動中だけ出します。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFootstepDustComponent> FootstepDustComponent;

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

	/** 疲労停止中のループ音です。 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> TiredLoopComponent;

	/** 警戒音を「認識していない→認識した」の変化でだけ鳴らすための前回状態です。 */
	bool bPlayerAwareForAudio = false;
	/** 警戒音の再生間隔を制御する最終再生時刻（秒）。 */
	double LastAlertSoundTime = -1.0e9;
};
