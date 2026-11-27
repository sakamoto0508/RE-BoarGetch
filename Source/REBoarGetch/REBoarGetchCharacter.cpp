// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBoarGetchCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "REBoarGetch.h"

AREBoarGetchCharacter::AREBoarGetchCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// 視点の操作でキャラクターが旋回しないよう、向きは移動方向に任せる。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// 移動の調整は派生Blueprintで上書きできるよう、ここでは既定値だけを設定する。
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// カメラが障害物にめり込まないよう、衝突時に縮むSpringArmを使う。
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// C++がアセットへ直接依存しないよう、MeshとAnim Blueprintは派生Blueprintで設定する。
}

void AREBoarGetchCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AREBoarGetchCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AREBoarGetchCharacter::Look);

		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AREBoarGetchCharacter::Look);
	}
	else
	{
		UE_LOG(LogREBoarGetch, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AREBoarGetchCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	DoMove(MovementVector.X, MovementVector.Y);
}

void AREBoarGetchCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AREBoarGetchCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// 視点が上下しても移動入力の基準は水平に保つため、Control RotationからYawだけを使う。
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AREBoarGetchCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AREBoarGetchCharacter::DoJumpStart()
{
	Jump();
}

void AREBoarGetchCharacter::DoJumpEnd()
{
	StopJumping();
}
