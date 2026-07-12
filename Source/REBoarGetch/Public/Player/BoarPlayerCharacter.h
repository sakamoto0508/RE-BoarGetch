#pragma once

#include "CoreMinimal.h"
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
	Interact,
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

public:

	///<summary> 移動。///</sumarry>
	void Move(const FVector2D& Input);

	///<summary> 視点操作。///</sumarry>
	void Look(const FVector2D& Input);

	///<summary> ジャンプスタート。///</sumarry>
	void StartJump();

	///<summary> ジャンプ終了。///</sumarry>
	void StopJump();

	///<summary> ダッシュ開始。///</summary>
	void StartDash();

	///<summary> ダッシュ終了。///</summary>
	void StopDash();

	///<summary> ガジェットを使用。///</summary>
	void UseGadget();

	///<summary> 指定スロットのガジェットへ切替。///</summary>
	void SwitchGadgetSlot(int32 SlotIndex);

	///<summary> 前方のオブジェクトとインタラクトします。///</summary>
	void Interact();

	///<summary> 捕獲開始。///</summary>
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

	// UFUNCTION(BlueprintPure, Category = "Player")
	// UCameraComponent* GetCameraComponentEx() const
	// {
	// 	return CameraComponentEx;
	// }

private:
	/// <summary>被弾判定用。イノシシ接触時にダメージ/スタン/無敵を開始します。</summary>
	UFUNCTION()
	void OnCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/// <summary>死亡イベント受信時にGameModeへ通知します。</summary>
	UFUNCTION()
	void OnDeath();

	/// <summary>移動停止を伴うスタン状態を開始します。</summary>
	void BeginStun();
	/// <summary>スタン終了後に通常移動へ復帰します。</summary>
	void EndStun();
	/// <summary>被弾直後の無敵時間を終了します。</summary>
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

	///<summary> 独自のカメラ制御用Component。（Aim・LockOnなどを担当）///</summary>
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	//TObjectPtr<UCameraComponent> CameraComponentEx;

	//-------------------------------------------------
	// Movement Settings
	//-------------------------------------------------
	
	///<summary> Yaw回転速度。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true"))
	float RotationRate = 720.f;

	///<summary> ジャンプ速度。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true"))
	float JumpVelocity = 700.f;

	///<summary> ジャンプ可能回数です（2で二段ジャンプ）。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 MaxJumpCountSetting = 2;

	///<summary> 空中での操作性。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement"
		,meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.35f;

	///<summary> 通常時の移動速度です。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float WalkSpeed = 500.0f;

	///<summary> ダッシュ時の移動速度です。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DashSpeed = 900.0f;

	///<summary> 被弾時のスタン秒数です。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StunDuration = 0.5f;

	///<summary> 被弾後の無敵秒数です。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float InvincibleDuration = 1.0f;

	///<summary> 被弾時に減るHP量です。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ContactDamage = 1.0f;

	///<summary> インタラクト判定距離です。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float InteractDistance = 250.0f;

	///<summary> インタラクト判定半径です。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float InteractRadius = 40.0f;
	
	///<summary> TPS用のカメラブームです。 </summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	///<summary> TPS用の追従カメラです。 </summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	///<summary> BPで調整しやすいカメラ距離です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
	float CameraArmLength = 350.0f;

	// 一時状態フラグ。入力・被弾処理のガード条件として使う。
	bool bIsDashing = false;
	bool bIsStunned = false;
	bool bIsInvincible = false;
	bool bHadMoveInput = false;
	EPlayerActionState CurrentActionState = EPlayerActionState::Idle;
	FTimerHandle StunTimerHandle;
	FTimerHandle InvincibleTimerHandle;
};