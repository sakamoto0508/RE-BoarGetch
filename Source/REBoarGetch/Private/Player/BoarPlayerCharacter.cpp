// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerCharacter.h"

#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Component/CaptureComponent.h"

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
	CameraComponentEx = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponentEx"));
	CameraComponentEx->SetupAttachment(GetRootComponent());

	//-------------------------------------------------
	// Character Movement
	//-------------------------------------------------

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	
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
	if (Controller == nullptr)	return;
	
	//Y入力が0に近い場合は無視。
	if (!FMath::IsNearlyZero(Input.Y))
	{
		AddMovementInput(GetActorForwardVector(), Input.Y);
	}
	
	//X入力が0に近い場合は無視。
	if (!FMath::IsNearlyZero(Input.X))
	{
		AddMovementInput(GetActorRightVector(), Input.X);
	}
}

void ABoarPlayerCharacter::Look(const FVector2D& Input)
{
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