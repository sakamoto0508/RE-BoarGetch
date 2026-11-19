#pragma once

#include "CoreMinimal.h"
#include "Audio/BoarAudioTypes.h"
#include "Gadget/GadgetBase.h"
#include "GameFramework/Character.h"
#include "BoarPlayerCharacter.generated.h"

class UHealthComponent;
class UGadgetComponent;
class UCaptureComponent;
class UPlayerHitReactionComponent;
class UCameraComponent;
class USpringArmComponent;
class UPrimitiveComponent;
class UAnimSequenceBase;
class UAnimMontage;
class ABoarBase;
class ANetGadget;
struct FBoarGadgetLoadoutContext;
struct FHitResult;

/**
 * ダッシュ用スタミナの変更通知です。HUDなど表示側はこの値を表示するだけで、計算はCharacterが行います。
 * @param bDashLocked スタミナ切れでDashを禁止中（再使用しきい値まで未回復）ならtrueです。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPlayerStaminaChanged, float, CurrentStamina, float, MaxStamina, bool, bDashLocked);

/**
 * ダメージが成立した被弾の情報です（AKI-163）。Gameplay側が確定した結果だけを演出側へ渡します。
 * 無敵中・攻撃判定外・Stage停止中など、ダメージが成立しなかった接触では作られません。
 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FBoarPlayerHitInfo
{
	GENERATED_BODY()

	/** 攻撃してきたActorです（突進中のBoar）。テスト等で直接Stunした場合はnullptrです。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Hit")
	TObjectPtr<AActor> Attacker = nullptr;

	/** 命中時の攻撃側の位置です。Camera Impulseの発生源に使います。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Hit")
	FVector AttackerLocation = FVector::ZeroVector;

	/** プレイヤー側の命中位置です（攻撃側を向いたCapsule表面・胴の高さ）。衝撃波の中心に使います。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Hit")
	FVector HitLocation = FVector::ZeroVector;

	/** 攻撃側からプレイヤーへ向かう水平方向（吹き飛ぶ向き）です。不明な場合はプレイヤーの後方です。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Hit")
	FVector HitDirection = FVector::ZeroVector;

	/** 命中時に空中にいたかです。空中被弾は着地してからしりもち・起き上がりへ進みます。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Hit")
	bool bAirborne = false;

	/** この命中でHP0になったか（GameOverのダウン）です。trueのときStunは始まらず、演出は衝撃波と頭上の星だけです（カメラ・SE等はGameOver演出側）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Hit")
	bool bLethal = false;
};

/** ダメージが成立した被弾を通知します。命中インパクト演出用です。HP0のダウンでも通知します（HitInfo.bLethal = true）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerHitReceived, const FBoarPlayerHitInfo&, HitInfo);
/** Stunが始まったことを通知します（操作不能の開始）。頭上の状態表示用です。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerStunStarted, const FBoarPlayerHitInfo&, HitInfo);
/**
 * Stunの残り時間の計測が始まったことを通知します。地上被弾では被弾直後、空中被弾では着地時です。
 * @param RecoveryDuration この通知から操作が戻るまでの秒数（しりもち→起き上がりの尺）です。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerStunRecoveryStarted, float, RecoveryDuration);
/**
 * Stunが終わったことを通知します。
 * @param bInterrupted ダウン（HP0）・Actor破棄などで時間満了前に打ち切られた場合はtrueです。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerStunEnded, bool, bInterrupted);

/** 空中アミ捕獲（Air Net Drop）の進行段階です。AnimBPや演出側から参照できます。 */
UENUM(BlueprintType)
enum class EAirNetDropPhase : uint8
{
	/** 空中アミ捕獲を実行していない状態です。 */
	None,
	/** 空中で一瞬ためる予備動作です。上昇を止めます。 */
	Anticipation,
	/** アミを前下へ振り下ろしながら急降下を開始する段階です。 */
	Swing,
	/** アミを前下へ構えたまま急降下している段階です。 */
	Drop,
	/** アミを引き戻して通常落下へ戻る段階です。急降下は終了済みです。 */
	Recovery,
	/** 着地の衝撃を受け止めて地上へ復帰する段階です。 */
	Landing
};

/** アニメーションや入力可否の判断に使用するプレイヤーの行動状態です。 */
UENUM(BlueprintType)
enum class EPlayerActionState : uint8
{
	/** 地上で入力がなく待機している状態です。 */
	Idle,
	/** 通常速度で地上移動している状態です。 */
	Walk,
	/** ダッシュ速度で地上移動している状態です。 */
	Run,
	/** 1回目のジャンプで上昇している状態です。 */
	Jump,
	/** 2回目のジャンプで上昇している状態です。 */
	DoubleJump,
	/** 空中を落下している状態です。 */
	Fall,
	/** 被弾によって操作不能になっている状態です。 */
	Stun,
	/** ガジェットを使用している状態です。 */
	UseGadget,
	/** 捕獲処理を実行している状態です。 */
	Capture,
	/** ポーズ中の表示状態。中断前の実行状態は保持します。 */
	Menu,
	/** HP0でダウンした状態。GameOver演出のダウンアニメを再生し、通常状態へ戻りません。 */
	Down
};

/**
 * プレイヤーキャラクターを管理するクラスです。
 *
 * 主に以下の処理を担当します。
 * ・カメラ基準のキャラクター移動
 * ・ジャンプと二段ジャンプ
 * ・ダッシュ
 * ・ガジェットの使用と切り替え
 * ・イノシシの捕獲
 * ・イノシシ接触時のダメージ、スタン、無敵時間
 * ・プレイヤー行動状態の管理
 */
UCLASS()
class REBOARGETCH_API ABoarPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	/** Component、カメラ、移動設定を含むプレイヤーの初期状態を構築します。 */
	ABoarPlayerCharacter();
	/** GameModeが解決した依存先を受け取り、HPと装備を一度だけ準備します。 */
	bool InitializeForPlay(const FBoarGadgetLoadoutContext& Context);
	bool IsReadyForPlay() const { return bStageInitialized; }

private:
	friend class FBoarGameplayRegressionTest;
	friend class FBoarNetStunInterruptTest;
	friend class FBoarPlayerStunLockTest;
	bool bStageInitialized = false;

protected:

	/** BP既定値（MaxStamina等）の適用後に、スタミナを最大値で初期化します。 */
	virtual void PostInitializeComponents() override;
	/** HPイベントと接触判定を登録し、ゲーム開始時の状態を初期化します。 */
	virtual void BeginPlay() override;
	/** ダッシュスタミナの消費・回復と、空中アミ捕獲中の急降下制御を更新します。 */
	virtual void Tick(float DeltaSeconds) override;
	/** 地上・上昇・落下への遷移に合わせてプレイヤー行動状態を更新します。 */
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	/** 空中被弾のStunは着地してから残り時間の計測を始めます。 */
	virtual void Landed(const FHitResult& Hit) override;
	/** Level遷移・Retry・破棄でStunのTimerと状態を残さず、演出側へ終了を通知します。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** ガジェット使用開始時のアニメ通知入口です（AnimBP側で実装）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Gadget")
	void OnGadgetUseStarted(EGadgetUseStyle UseStyle, AGadgetBase* Gadget);

	/** ガジェット使用終了時のアニメ通知入口です（AnimBP側で実装）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Gadget")
	void OnGadgetUseStopped(EGadgetUseStyle UseStyle, AGadgetBase* Gadget);

	/** ガジェット使用アニメーションの終了時に呼び出します。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Gadget")
	void FinishGadgetUseAnimation(bool bWasInterrupted);

	/** ガジェット使用アニメーションが中断されたことをAnimBPへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Animation")
	void OnGadgetUseInterrupted();

public:

	/** 移動。 */
	void Move(const FVector2D& Input);

	/** 視点操作。 */
	void Look(const FVector2D& Input);

	/** ジャンプスタート。 */
	void StartJump();

	/** ジャンプ終了。 */
	void StopJump();

	/** ダッシュ開始。 */
	void StartDash();

	/** ダッシュ終了。 */
	void StopDash();

	/** ガジェット使用開始。 */
	void StartGadgetUse();

	/** ガジェット使用終了。 */
	void StopGadgetUse();
	/** 装備変更・Stage終了から既存の中断経路を呼び出します。 */
	void InterruptGadgetUse() { EndGadgetUse(true); }

	/**
	 * 網の捕獲判定窓を今開いてよいかを返します。AnimNotifyとNetGadgetの両方から確認します。
	 * GadgetBase::IsUsingはOneShotで開始直後にfalseへ戻るため使わず、Character側の使用状態で判断します。
	 * @param Gadget 判定窓を開こうとしている網です。現在装備中のガジェットと一致する必要があります。
	 * @param MontageInstanceID Notify元のMontage再生インスタンスです。不明ならINDEX_NONEを渡します。
	 */
	bool CanOpenNetCaptureWindow(const ANetGadget* Gadget, int32 MontageInstanceID = INDEX_NONE) const;

	/** 指定スロットのガジェットへ切替（プレイ中のクイック切替）。Hold使用中は使用を終了してから切り替えます。 */
	void SwitchGadgetSlot(int32 SlotIndex);

	/** 捕獲開始。 */
	/**
	 * 将来の「プレイヤーが捕獲される」ルール用入口です。
	 * 現在のゲームプレイ経路からは未使用ですが、Capture状態とComponentを一式で残しています。
	 */
	void Capture();

	//-------------------------------------------------
	// Getter
	//-------------------------------------------------

	/** プレイヤーのHP管理Componentを取得します。 */
	UFUNCTION(BlueprintPure, Category = "Player")
	UHealthComponent* GetHealthComponent() const
	{
		return HealthComponent;
	}

	/** プレイヤーのガジェット管理Componentを取得します。 */
	UFUNCTION(BlueprintPure, Category = "Player")
	UGadgetComponent* GetGadgetComponent() const
	{
		return GadgetComponent;
	}

	/** プレイヤーの捕獲管理Componentを取得します。 */
	UFUNCTION(BlueprintPure, Category = "Player")
	UCaptureComponent* GetCaptureComponent() const
	{
		return CaptureComponent;
	}

	/** 現在のプレイヤー行動状態を取得します。 */
	UFUNCTION(BlueprintPure, Category = "Player")
	EPlayerActionState GetPlayerActionState() const
	{
		return bMenuOpen ? EPlayerActionState::Menu : CurrentActionState;
	}
	void SetMenuOpen(bool bOpen) { bMenuOpen = bOpen; }

	/** HP0でダウンしているかを返します。ダウン後はStun・移動・ガジェットへ戻りません。 */
	UFUNCTION(BlueprintPure, Category = "Player")
	bool IsDowned() const { return bIsDowned; }

	/** 被弾Stun中（操作不能）かを返します。移動・ダッシュ・ジャンプ・ガジェットの可否はこの状態を唯一の根拠にします。 */
	UFUNCTION(BlueprintPure, Category = "Player|Stun")
	bool IsStunned() const { return bIsStunned; }

	/** 被弾演出を担当するComponentを取得します。 */
	UFUNCTION(BlueprintPure, Category = "Player")
	UPlayerHitReactionComponent* GetHitReactionComponent() const { return HitReactionComponent; }

	/** ダメージが成立した被弾（HP0のダウンを除く）の通知です。演出はこの結果を受けて再生します。 */
	UPROPERTY(BlueprintAssignable, Category = "Player|Stun")
	FOnPlayerHitReceived OnPlayerHitReceived;

	/** Stun（操作不能）の開始通知です。 */
	UPROPERTY(BlueprintAssignable, Category = "Player|Stun")
	FOnPlayerStunStarted OnStunStarted;

	/** Stunの残り時間の計測開始通知です（地上被弾は即時、空中被弾は着地時）。 */
	UPROPERTY(BlueprintAssignable, Category = "Player|Stun")
	FOnPlayerStunRecoveryStarted OnStunRecoveryStarted;

	/** Stunの終了通知です。中断（ダウン・破棄）でも必ず1回届きます。 */
	UPROPERTY(BlueprintAssignable, Category = "Player|Stun")
	FOnPlayerStunEnded OnStunEnded;

	/** ガジェット使用中かを返します。 */
	UFUNCTION(BlueprintPure, Category = "Player|Gadget")
	bool IsGadgetInUse() const
	{
		return bIsGadgetInUse;
	}

	/** 現在ガジェットの使用タイプを返します。 */
	UFUNCTION(BlueprintPure, Category = "Player|Gadget")
	EGadgetUseStyle GetCurrentGadgetUseStyle() const
	{
		return CurrentGadgetUseStyle;
	}

	/** 空中アミ捕獲の進行段階を返します。使用開始時に空中だった場合だけNone以外になります。 */
	UFUNCTION(BlueprintPure, Category = "Player|Gadget|AirNetDrop")
	EAirNetDropPhase GetAirNetDropPhase() const { return AirNetDropPhase; }

	/** 空中アミ捕獲を実行中かを返します。 */
	UFUNCTION(BlueprintPure, Category = "Player|Gadget|AirNetDrop")
	bool IsAirNetDropActive() const { return AirNetDropPhase != EAirNetDropPhase::None; }

	//-------------------------------------------------
	// ---- ダッシュとスタミナ ----
	//-------------------------------------------------

	/** スタミナの値またはDash禁止状態が変わったときに通知します。 */
	UPROPERTY(BlueprintAssignable, Category = "Player|Stamina")
	FOnPlayerStaminaChanged OnStaminaChanged;

	/** 現在のスタミナです。 */
	UFUNCTION(BlueprintPure, Category = "Player|Stamina")
	float GetCurrentStamina() const { return CurrentStamina; }

	/** スタミナの最大値です。 */
	UFUNCTION(BlueprintPure, Category = "Player|Stamina")
	float GetMaxStamina() const { return MaxStamina; }

	/** スタミナ切れでDashを禁止中か（再使用しきい値まで未回復か）を返します。 */
	UFUNCTION(BlueprintPure, Category = "Player|Stamina")
	bool IsDashLockedByExhaustion() const { return bDashExhausted; }

	/** Dash速度を適用中かを返します。Dash入力の押下状態ではありません。 */
	UFUNCTION(BlueprintPure, Category = "Player|Stamina")
	bool IsDashing() const { return bIsDashing; }

	/** Playerの移動SE設定です（足音はAnimNotify_BoarFootstepから参照します）。 */
	const FBoarPlayerAudioSettings& GetPlayerAudio() const { return PlayerAudio; }

	/** Player位置から位置付きSEを一度鳴らします（BoarAudioManagerSubsystem経由。未設定なら何もしません）。 */
	void PlayPlayerSound(USoundBase* Sound, float VolumeMultiplier = 1.0f) const;

private:
	/** ジャンプ・着地・ダッシュ・足音のSEです。Gameplayの判定には使いません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Audio", meta = (AllowPrivateAccess = "true"))
	FBoarPlayerAudioSettings PlayerAudio;

	bool bMenuOpen = false;
	/** 被弾判定用。イノシシ接触時にダメージ/スタン/無敵を開始します。 */
	UFUNCTION()
	void OnCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** PlayerとBoarのCapsuleは互いにPawnをBlockするためOverlapが発生しません。Blocking Hitも同じ接触判定へ渡します。 */
	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	/** Boarとの接触でダメージ/スタン/無敵（HP0ではダウン）を開始します。 */
	void HandleBoarContact(AActor* OtherActor);

	/** 死亡イベント受信時にGameModeへ通知します。 */
	UFUNCTION()
	void OnDeath();

	/**
	 * 操作不能のスタン状態を開始します。
	 * 入力はIsGameplayActionBlockedで一元的に拒否し、CharacterMovementは止めずに重力・ノックバック・着地を処理させます
	 * （DisableMovementは空中で静止してしまい、Stage停止など他の所有者の移動停止と区別できないため使いません）。
	 */
	void BeginStun(const FBoarPlayerHitInfo& HitInfo);
	/** 攻撃元のない直接のStun開始です（テスト・既存呼び出し用）。 */
	void BeginStun();

	/** Stunの残り時間（StunDuration）の計測を始めます。空中被弾では着地時、または着地待ちの上限で呼ばれます。 */
	void StartStunRecovery();

	/** HP0時にStunの代わりに呼び、行動を止めてダウン状態にします（ダウンアニメはGameOver演出が再生）。 */
	void EnterDowned();

	/** スタン時間の満了で通常移動へ復帰します。 */
	void EndStun();

	/**
	 * Stunを終了します。正常終了・中断のどちらからも呼ばれ、何度呼んでも安全です。
	 * 自分が変更した回転設定だけを戻し、Stage停止など他の所有者による移動停止・入力停止は解除しません。
	 */
	void FinishStun(bool bInterrupted);

	/** 被弾直後の無敵時間を終了します。 */
	void EndInvincible();

	/** ガジェット使用終了処理。 */
	void EndGadgetUse(bool bWasInterrupted);

	/**  地上での入力状態に応じてIdle・Walk・Runを設定します。 */
	void UpdateGroundActionState(bool bHasMoveInput);

	/** プレイヤーの現在行動状態を変更します。 */
	void SetPlayerActionState(EPlayerActionState NewState);

	/**
	 * Gadgetの使用状態に関係なく、すべての行動入力を止める状態か確認します（Stage停止・Stun・Downed）。
	 * 操作ごとの許可判定は必ずこの条件を含みます。
	 */
	bool IsGameplayActionBlocked() const;

	/** 移動・ジャンプ・ダッシュを受け付けられるか確認します。使用中は開始時に保存したCanMoveWhileUsingに従います。 */
	bool CanUseMovementActions() const;

	/**
	 * プレイ中のクイックSlot切替を受け付けられるか確認します。
	 * Hold使用中は終了してから切り替え、OneShot使用中（アミ等）は従来どおり拒否します。
	 * Pause Menuの装備変更（UGadgetComponent::SetGadgetSlot）はこの判定を通りません。
	 */
	bool CanSwitchGadgetSlot() const;

	/** Gadget使用開始などGadget使用中は受け付けない行動入力を禁止する状態か確認します。 */
	bool IsActionLocked() const;

	//-------------------------------------------------
	// Air Net Drop（空中アミ捕獲）
	//-------------------------------------------------

	/** 装備と移動状態だけから使用方式を決定します。演出欠落で地上用へ切り替えません。 */
	bool ShouldStartAirNetDrop(const AGadgetBase* Gadget) const;

	/** 使用開始前にNotifyを持つ必須Animationと再生先を検証します。副作用はありません。 */
	bool HasAirNetDropPresentation() const;

	/** 空中アミ捕獲の移動制御を開始し、予備動作を再生します。再生できない場合はfalseです。 */
	bool BeginAirNetDrop();

	/** 予備動作→振り下ろし→急降下→引き戻しの時間経過による段階遷移と、急降下の速度制御を行います。 */
	void TickAirNetDrop(float DeltaSeconds);

	/** 段階を切り替え、その段階のAnimationを再生します。再生できない場合はfalseです。 */
	bool EnterAirNetDropPhase(EAirNetDropPhase NewPhase);

	/** 急降下を開始します（上昇速度を打ち消し、初速を下向きに与えます）。 */
	void BeginAirNetDropDive();

	/** 急降下の時間上限で判定窓を閉じ、移動設定を戻して引き戻しへ進みます。 */
	void BeginAirNetDropRecovery();

	/** 着地時に急降下を止め、着地Animationへ切り替えます。 */
	void HandleAirNetDropLanded();

	/** 空中アミ捕獲で変更したCharacterMovementの値を元に戻します。複数回呼んでも安全です。 */
	void RestoreAirNetDropMovement();

	/** 空中アミ捕獲を終了します（移動値の復元・Montage停止）。EndGadgetUseの全経路から呼ばれ、複数回呼んでも安全です。 */
	void EndAirNetDrop();

	/** 空中アミ捕獲のAnimationをFullBodyスロットで再生し、終了通知を登録します。 */
	bool PlayAirNetDropAnimation(UAnimSequenceBase* Animation, float PlayRate, int32 LoopCount, float StartTime);

	/** 再生中の空中アミ捕獲Montageの終了通知を外します。段階切替時の割り込みを中断扱いにしないためです。 */
	void ClearAirNetDropMontageDelegate();

	/** 空中アミ捕獲Montageの終了通知です。引き戻し・着地の完了でガジェット使用を終了し、外部からの割り込みでは中断終了します。 */
	void OnAirNetDropMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** 現在装備中の網ガジェットを返します。網以外ならnullptrです。 */
	ANetGadget* GetEquippedNetGadget() const;

	/** 空中アミ捕獲：予備動作のAnimationです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequenceBase> AirNetDropAnticipationAnimation;

	/** 空中アミ捕獲：前下への振り下ろしAnimationです。Capture ON NotifyはこのAnimation上にあります。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequenceBase> AirNetDropSwingAnimation;

	/** 空中アミ捕獲：急降下中にループする構えAnimationです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequenceBase> AirNetDropLoopAnimation;

	/** 空中アミ捕獲：アミを引き戻して通常落下へ戻るAnimationです。Capture OFF Notifyを含みます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequenceBase> AirNetDropRecoveryAnimation;

	/** 空中アミ捕獲：着地Animationです。冒頭にアミを地面へ叩き付けるCapture ON/OFF Notifyを含みます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequenceBase> AirNetDropLandAnimation;

	/** 空中アミ捕獲のAnimationを再生するスロット名です（ABP_PlayerのSlotノード）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop|Animation", meta = (AllowPrivateAccess = "true"))
	FName AirNetDropSlotName = TEXT("FullBody");

	/** 段階間・開始終了時のブレンド秒数です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropBlendTime = 0.08f;

	/** 予備動作の秒数です。予備動作Animationはこの秒数に合わせて再生速度を変えます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.02"))
	float AirNetDropAnticipationDuration = 0.12f;

	/** 予備動作中の上昇速度の上限(cm/s)です。ジャンプ上昇中に発動しても上昇が続かないようにします。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropAnticipationMaxRiseSpeed = 120.0f;

	/** 予備動作中の重力倍率（元のGravityScaleに乗算）です。小さいほど一瞬ためる感じが強くなります。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropAnticipationGravityMultiplier = 0.35f;

	/** 振り下ろしAnimationの再生速度です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float AirNetDropSwingPlayRate = 1.15f;

	/** 急降下開始時の下向き初速(cm/s)です。すでにこれより速く落下している場合はその速度を維持します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropInitialDownSpeed = 900.0f;

	/** 急降下中の下向き加速度(cm/s^2)です。急降下中は通常重力の代わりにこの値で加速します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropDownAcceleration = 5200.0f;

	/** 急降下中の最大落下速度(cm/s)です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropMaxFallSpeed = 2400.0f;

	/** 急降下を続ける最大秒数（振り下ろし開始から）です。高所では経過後に引き戻して通常落下へ戻ります。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.05"))
	float AirNetDropMaxDiveDuration = 0.65f;

	/** 空中アミ捕獲中のAirControl倍率です（元のAirControlに乗算）。0で空中の操作入力を受け付けません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropHorizontalControlMultiplier = 0.4f;

	/** 急降下開始時に水平速度へ掛ける倍率です。1で発動時の水平移動をそのまま維持します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float AirNetDropHorizontalVelocityScale = 0.75f;

	/** 着地Animationを何秒で終えるかです。着地Animationはこの秒数に合わせて再生速度を変えます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.05"))
	float AirNetDropLandingRecoveryDuration = 0.35f;

	/**
	 * 捕獲判定窓が閉じた後（引き戻し中）に着地した場合の、着地Animationの開始位置（秒）です。
	 * 冒頭の叩き付けNotify(ON/OFF)より後ろから始め、閉じた判定窓を再び開かないようにします。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|AirNetDrop", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AirNetDropLandingSkipCaptureTime = 0.15f;

	/** 現在の空中アミ捕獲の段階です。 */
	EAirNetDropPhase AirNetDropPhase = EAirNetDropPhase::None;
	/** 現在の段階に入ってからの経過秒数です。 */
	float AirNetDropPhaseElapsed = 0.0f;
	/** 急降下（振り下ろし開始）からの経過秒数です。 */
	float AirNetDropDiveElapsed = 0.0f;
	/** 空中アミ捕獲で変更したCharacterMovementの値を保持中かです。 */
	bool bAirNetDropMovementOverridden = false;
	/** 開始前のGravityScaleです。 */
	float SavedAirNetDropGravityScale = 1.0f;
	/** 開始前のAirControlです。 */
	float SavedAirNetDropAirControl = 0.0f;
	/** 現在の段階で再生しているMontageです。段階切替や外部の割り込みを識別します。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> AirNetDropMontage;

	/** 地上のガジェット使用でBP（OnGadgetUseStarted）が開始したMontageです。中断時に止める対象を限定します。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> GadgetUseMontage;
	/** GadgetUseMontageの再生インスタンスIDです。古いインスタンスのNotify・終了通知を今回の使用と区別します。 */
	int32 GadgetUseMontageInstanceID = INDEX_NONE;

	/** 今回の使用で開始した地上Montageだけを停止し、記録を消します。 */
	void StopGadgetUseMontage();

	//-------------------------------------------------
	// Components
	//-------------------------------------------------

	/** プレイヤーのHPと死亡通知を管理するComponentです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHealthComponent> HealthComponent;

	/** ガジェットのスロット、装備、使用状態を管理するComponentです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGadgetComponent> GadgetComponent;

	/** プレイヤー自身の捕獲状態を管理するComponentです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/** 被弾・Stunの演出（衝撃波・Hit Flash・SE・Camera・しりもちAnimation・頭上VFX）を担当するComponentです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPlayerHitReactionComponent> HitReactionComponent;

	/** 独自のカメラ制御用Component。（Aim・LockOnなどを担当） */
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	//TObjectPtr<UCameraComponent> CameraComponentEx;

	//-------------------------------------------------
	// ---- 移動の設定 ----
	//-------------------------------------------------

	/** Yaw回転速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true"))
	float RotationRate = 720.f;

	/** ジャンプ速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true"))
	float JumpVelocity = 700.f;

	/** ジャンプ可能回数です（2で二段ジャンプ）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 MaxJumpCountSetting = 2;

	/** 空中での操作性。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement"
		,meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.35f;

	/** 通常時の移動速度です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float WalkSpeed = 500.0f;

	/** ダッシュ時の移動速度です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DashSpeed = 900.0f;

	/** スタミナの最大値です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MaxStamina = 100.0f;

	/** 地上で実際にDash移動している間の消費量(/秒)です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DashDrainPerSecond = 25.0f;

	/** 回復開始後の回復量(/秒)です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaRegenPerSecond = 20.0f;

	/** 最後に実際に消費してから回復を始めるまでの秒数です。Pause中は進みません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaRegenDelay = 0.7f;

	/** スタミナ切れ後、Dashを再使用できるようになる残量（最大値に対する割合）です。再使用にはDash入力の押し直しも必要です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float DashRestartThresholdRatio = 0.25f;

	/**
	 * 消費対象とみなす最低の水平速度(cm/s)です。壁に向かって押し付けているだけ・停止中は実速度がこれを下回り、消費しません。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DashDrainMinSpeed = 50.0f;

	/**
	 * 被弾時のスタン秒数です（地上被弾は被弾から、空中被弾は着地から数えます）。
	 * しりもち→起き上がりのAnimationはこの秒数に合わせて再生速度を変えるため、起き上がり完了と操作復帰が一致します。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Stun",meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "s"))
	float StunDuration = 1.6f;

	/** 空中被弾で着地を待つ最大秒数です。高所・落下継続でもこの秒数を過ぎたらスタンの残り時間を数え始めます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Stun",meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "s"))
	float StunAirborneMaxWait = 1.5f;

	/** 地上被弾で攻撃方向へ押し出す初速(cm/s)です。入力なしの通常の減速で止まります。0でその場に止まります。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Stun",meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm/s"))
	float StunKnockbackSpeed = 380.0f;

	/** 空中被弾で攻撃方向へ流す水平速度(cm/s)です。縦の速度（落下）はそのまま維持します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Stun",meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm/s"))
	float StunAirborneKnockbackSpeed = 220.0f;

	/** 被弾時に攻撃してきた方向へ体を向けます。後ろへ倒れるリアクションで攻撃方向が伝わるようにします。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Stun",meta = (AllowPrivateAccess = "true"))
	bool bFaceAttackerOnStun = true;

	/** 被弾から数える無敵秒数の下限です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "s"))
	float InvincibleDuration = 1.0f;

	/** スタン終了（起き上がり）後に続ける無敵秒数です。起き上がった瞬間の再被弾を防ぎます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "s"))
	float PostStunInvincibleDuration = 0.6f;

	/** 被弾時に減るHP量です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ContactDamage = 1.0f;

	/** TPS用のカメラブームです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** TPS用の追従カメラです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	/** BPで調整しやすいカメラ距離です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
	float CameraArmLength = 350.0f;

	// 一時状態フラグ。入力・被弾処理のガード条件として使う。
	/**
	 * 現在ダッシュ速度を適用しているかを表します（Dash入力の押下状態とは別）。
	 * スタミナ切れではキーを押したままでもfalseへ戻り、押し直し（StartDash）以外では再びtrueになりません。
	 */
	bool bIsDashing = false;

	/** 現在のスタミナです。PostInitializeComponentsでMaxStaminaに初期化します。 */
	float CurrentStamina = 100.0f;
	/** 最後に実際に消費してからの経過秒数です（Tickの経過時間で進むためPause中は止まります）。 */
	float TimeSinceStaminaDrain = 0.0f;
	/** スタミナ切れでDashを終了し、再使用しきい値まで回復していない状態です。 */
	bool bDashExhausted = false;
	/** 最後に通知した値です。変化したときだけOnStaminaChangedを発火します。 */
	float LastBroadcastStamina = -1.0f;
	bool bLastBroadcastDashLocked = false;

	/** スタミナの消費・回復・枯渇判定を1フレーム分進めます。 */
	void TickStamina(float DeltaSeconds);
	/** 地上で移動入力があり、実際にDash移動が成立しているときだけtrueを返します。 */
	bool ShouldDrainDashStamina() const;
	/** スタミナ切れによる禁止中でなく、残量があればDashを開始できます。 */
	bool CanStartDash() const;
	/** スタミナ切れでDashを終了します。入力の押下状態は変えず、押し直すまで再開しません。 */
	void EndDashByExhaustion();
	/** Dash状態に応じたWalk/Dash速度をMaxWalkSpeedへ適用する唯一の経路です。 */
	void ApplyGroundMoveSpeed();
	/** 値または禁止状態が変化した場合（bForceなら常に）OnStaminaChangedを発火します。 */
	void BroadcastStaminaChanged(bool bForce = false);
	/** スタン中で移動やガジェット入力を受け付けない状態かを表します。 */
	bool bIsStunned = false;
	/** 接触ダメージを重複して受けない無敵時間中かを表します。 */
	bool bIsInvincible = false;
	/** HP0でダウンした状態です。Stun終了Timer等で通常状態へ戻さないためのガードです。 */
	bool bIsDowned = false;
	/** ガジェットの開始から共通終了処理までの使用中状態を表します。 */
	bool bIsGadgetInUse = false;
	/** 直前の移動更新で入力が存在したかを保持し、状態更新の重複を抑えます。 */
	bool bHadMoveInput = false;
	/** 現在使用しているガジェットの使用形式です。 */
	EGadgetUseStyle CurrentGadgetUseStyle = EGadgetUseStyle::OneShot;
	/**
	 * 今回の使用が移動・ジャンプ・ダッシュを許可するかです。使用開始時にAGadgetBase::CanMoveWhileUsingから確定し、
	 * 使用中に装備が入れ替わっても判定が変わらないようCharacter側へ保存します。空中アミでは常にfalseです。
	 */
	bool bGadgetUseAllowsMovement = false;
	/** 現在のプレイヤー行動状態です。 */
	EPlayerActionState CurrentActionState = EPlayerActionState::Idle;
	/** スタン終了処理を実行するTimerの識別子です。 */
	FTimerHandle StunTimerHandle;
	/** 空中被弾で着地を待つ上限のTimerです。 */
	FTimerHandle StunAirborneWaitTimerHandle;
	/** 空中被弾で、着地（残り時間の計測開始）を待っている間trueです。 */
	bool bStunAwaitingLanding = false;
	/** Stun開始前のbOrientRotationToMovementです。Stun中は向きを固定し、終了時にこの値へ戻します。 */
	bool bStunSavedOrientRotationToMovement = true;
	/** 無敵時間終了処理を実行するTimerの識別子です。 */
	FTimerHandle InvincibleTimerHandle;
};
