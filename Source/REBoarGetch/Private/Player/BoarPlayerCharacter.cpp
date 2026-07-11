// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerCharacter.h"

#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Component/CaptureComponent.h"

#include "Engine/Engine.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ABoarPlayerCharacter::ABoarPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	//-------------------------------------------------
	// Components
	//-------------------------------------------------

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	GadgetComponent = CreateDefaultSubobject<UGadgetComponent>(TEXT("GadgetComponent"));
	CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
	//CameraComponentEx = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponentEx"));
	//CameraComponentEx->SetupAttachment(GetRootComponent());

	/// TPS移動の基本設定
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	// SpringArm
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = CameraArmLength;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 10.0f;

	// Camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	
	GetCharacterMovement()->RotationRate = FRotator(0.f, RotationRate, 0.f);

	GetCharacterMovement()->JumpZVelocity = JumpVelocity;
	GetCharacterMovement()->AirControl = AirControl;
}

void ABoarPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void ABoarPlayerCharacter::Move(const FVector2D& Input)
{
	if (Controller == nullptr) return;

	const FRotator ControlRot = Controller->GetControlRotation();
	const FRotator YawRot(0.f, ControlRot.Yaw, 0.f);

	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	if (!FMath::IsNearlyZero(Input.Y))
	{
		AddMovementInput(Forward, Input.Y);
	}
	if (!FMath::IsNearlyZero(Input.X))
	{
		AddMovementInput(Right, Input.X);
	}
}

void ABoarPlayerCharacter::Look(const FVector2D& Input)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			2.f,
			FColor::Green,
			FString::Printf(TEXT("Look X: %.2f  Y: %.2f"), Input.X, Input.Y)
		);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Look X is false"));
	}
	
	if (Controller == nullptr) return;
	
	if (!FMath::IsNearlyZero(Input.X))
	{
		AddControllerYawInput(Input.X);
	}

	if (!FMath::IsNearlyZero(Input.Y))
	{
		AddControllerPitchInput(Input.Y);
	}
}

void ABoarPlayerCharacter::StartJump()
{
	Jump();
}

void ABoarPlayerCharacter::StopJump()
{
	StopJumping();
}

void ABoarPlayerCharacter::UseGadget()
{
	if (GadgetComponent)
	{
		GadgetComponent->UseCurrentGadget();
	}
}

void ABoarPlayerCharacter::Capture()
{
	if (CaptureComponent)
	{
		CaptureComponent->Capture(this);
	}
}