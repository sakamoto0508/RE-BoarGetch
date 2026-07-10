#include "public/Player/BoarPlayerCharacter.h"

#include "public/Component/HealthComponent.h"
#include "public/Component/GadgetComponent.h"

#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// Sets default values
ABoarPlayerCharacter::ABoarPlayerCharacter()
{
	//このCharacter事態は毎フレーム計算しなくてよいのでTickは切る。
 	PrimaryActorTick.bCanEverTick=false;
	
	// C++ 側でコンポーネントを作っておくと、BP では「このクラスを土台にする」だけで済みます。
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	GadgetComponent = CreateDefaultSubobject<UGadgetComponent>(TEXT("GadgetComponent"));
	
	//プレイヤーキャラクターの基本設定。
	bUseControllerRotationPitch=false;
	bUseControllerRotationYaw=false;
	bUseControllerRotationRoll=false;
	
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = true;
		Movement->RotationRate = FRotator(0.0f, RotationRateYaw, 0.0f);
		Movement->JumpZVelocity = JumpZVelocity;
		Movement->AirControl = AirControl;
	}
}

void ABoarPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	// ここでは無理に処理を盛らない。
	// 初期値やゲーム開始時の装備は、後で GameInstance や Save から流し込む想定。
}

void ABoarPlayerCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	//まずはシンプルな入力。
	//InputSettingsに以下の名前を作っておく前提。
	// -MoveForward
	// -MoveRight
	// -Turn
	// -lookUp
	// -Jump
	
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ABoarPlayerCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ABoarPlayerCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ABoarPlayerCharacter::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ABoarPlayerCharacter::LookUp);

	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ABoarPlayerCharacter::StartJump);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ABoarPlayerCharacter::StopJump);
}


void ABoarPlayerCharacter::MoveForward(float Value)
{
	if (Controller == nullptr || FMath::IsNearlyZero(Value))
	{
		return;
	}

	// カメラの向きを基準に前後移動する。
	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

	AddMovementInput(ForwardDirection, Value);
}

void ABoarPlayerCharacter::MoveRight(float Value)
{
	if (Controller == nullptr || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(RightDirection, Value);
}

void ABoarPlayerCharacter::Turn(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	AddControllerYawInput(Value);
}

void ABoarPlayerCharacter::LookUp(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	AddControllerPitchInput(Value);
}

void ABoarPlayerCharacter::StartJump()
{
	Jump();
}

void ABoarPlayerCharacter::StopJump()
{
	StopJumping();
}

