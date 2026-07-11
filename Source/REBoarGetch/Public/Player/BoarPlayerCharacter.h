#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoarPlayerCharacter.generated.h"

class UHealthComponent;
class UGadgetComponent;
class UCaptureComponent;
class UCameraComponent;

UCLASS()
class REBOARGETCH_API ABoarPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	ABoarPlayerCharacter();

protected:

	virtual void BeginPlay() override;

public:

	///<summary> 移動。///</sumarry>
	void Move(const FVector2D& Input);

	///<summary> 視点操作。///</sumarry>
	void Look(const FVector2D& Input);

	///<summary> ジャンプスタート。///</sumarry>
	void StartJump();

	///<summary> ジャンプ終了。///</sumarry>
	void StopJump();

	///<summary> ガジェットを使用。///</summary>
	void UseGadget();

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
	UCameraComponent* GetCameraComponentEx() const
	{
		return CameraComponentEx;
	}

private:

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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> CameraComponentEx;

	//-------------------------------------------------
	// Movement Settings
	//-------------------------------------------------
	
	///<summary> Yaw回転速度。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true"))
	float RotationRate = 720.f;

	///<summary> ジャンプ速度。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement",meta = (AllowPrivateAccess = "true"))
	float JumpVelocity = 700.f;

	///<summary> 空中での操作性。///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement"
		,meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.35f;
};