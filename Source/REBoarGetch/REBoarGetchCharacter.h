// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "REBoarGetchCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/** 三人称キャラクターのカメラ追従と基本入力を担当する。 */
UCLASS(abstract)
class AREBoarGetchCharacter : public ACharacter
{
	GENERATED_BODY()

	/** 障害物を避けながら追従カメラをキャラクター後方に保持するSpringArm。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** SpringArmの先端からキャラクターを追従するカメラ。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** ジャンプの開始・終了を受け取る入力Action。 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** カメラ基準の水平移動を受け取る2軸入力Action。 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** スティック等の視点操作を受け取る2軸入力Action。 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** マウスの移動量で視点を操作する2軸入力Action。 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	AREBoarGetchCharacter();	

protected:

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	void Move(const FInputActionValue& Value);

	void Look(const FInputActionValue& Value);

public:

	/** 入力デバイスやUIから受けた軸入力を、カメラ基準の水平移動へ変換する。 */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** 入力デバイスやUIから受けた軸入力を、Controllerの視点操作へ渡す。 */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

