#pragma once

#include "CoreMinimal.h"
#include "Gadget/GadgetBase.h"
#include "GameFramework/Character.h"
#include "BoarPlayerCharacter.generated.h"

class UHealthComponent;
class UGadgetComponent;
class UCaptureComponent;
class UCameraComponent;
class USpringArmComponent;
class UCameraComponent;
class UPrimitiveComponent;
class ABoarBase;
struct FHitResult;

UENUM(BlueprintType)
enum class EPlayerActionState : uint8
{
	Idle,
	Walk,
	Run,
	Jump,
	DoubleJump,
	Fall,
	Stun,
	UseGadget,
	Capture
};

UCLASS()
class REBOARGETCH_API ABoarPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	ABoarPlayerCharacter();

protected:

	virtual void BeginPlay() override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

	/** ガジェット使用開始時のアニメ通知入口です（AnimBP側で実装）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Gadget")
	void OnGadgetUseStarted(EGadgetUseStyle UseStyle, AGadgetBase* Gadget);

	/** ガジェット使用終了時のアニメ通知入口です（AnimBP側で実装）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Player|Gadget")
	void OnGadgetUseStopped(EGadgetUseStyle UseStyle, AGadgetBase* Gadget);

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

	/** ガジェットを使用。 */
	void UseGadget();

	/** ガジェット使用開始。 */
	void StartGadgetUse();

	/** ガジェット使用終了。 */
	void StopGadgetUse();

	/** 指定スロットのガジェットへ切替。 */
	void SwitchGadgetSlot(int32 SlotIndex);

	/** 捕獲開始。 */
	void Capture();

	//-------------------------------------------------
	// Getter
	//-------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Player")
	UHealthComponent* GetHealthComponent() const
	{
		return HealthComponent;
	}

	UFUNCTION(BlueprintPure, Category = "Player")
	UGadgetComponent* GetGadgetComponent() const
	{
		return GadgetComponent;
	}

	UFUNCTION(BlueprintPure, Category = "Player")
	UCaptureComponent* GetCaptureComponent() const
	{
		return CaptureComponent;
	}

	UFUNCTION(BlueprintPure, Category = "Player")
	EPlayerActionState GetPlayerActionState() const
	{
		return CurrentActionState;
	}

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

	// UFUNCTION(BlueprintPure, Category = "Player")
	// UCameraComponent* GetCameraComponentEx() const
	// {
	// 	return CameraComponentEx;
	// }

private:
	/** 被弾判定用。イノシシ接触時にダメージ/スタン/無敵を開始します。 */
	UFUNCTION()
	void OnCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 死亡イベント受信時にGameModeへ通知します。 */
	UFUNCTION()
	void OnDeath();

	/** 移動停止を伴うスタン状態を開始します。 */
	void BeginStun();
	/** スタン終了後に通常移動へ復帰します。 */
	void EndStun();
	/** 被弾直後の無敵時間を終了します。 */
	void EndInvincible();
	void UpdateGroundActionState(bool bHasMoveInput);
	void SetPlayerActionState(EPlayerActionState NewState);
	bool IsActionLocked() const;

	//-------------------------------------------------
	// Components
	//-------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGadgetComponent> GadgetComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/** 独自のカメラ制御用Component。（Aim・LockOnなどを担当） */
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	//TObjectPtr<UCameraComponent> CameraComponentEx;

	//-------------------------------------------------
	// Movement Settings
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

	/** 被弾時のスタン秒数です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StunDuration = 0.5f;

	/** 被弾後の無敵秒数です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float InvincibleDuration = 1.0f;

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
	bool bIsDashing = false;
	bool bIsStunned = false;
	bool bIsInvincible = false;
	bool bIsGadgetInUse = false;
	bool bHadMoveInput = false;
	EGadgetUseStyle CurrentGadgetUseStyle = EGadgetUseStyle::OneShot;
	EPlayerActionState CurrentActionState = EPlayerActionState::Idle;
	FTimerHandle StunTimerHandle;
	FTimerHandle InvincibleTimerHandle;
};
