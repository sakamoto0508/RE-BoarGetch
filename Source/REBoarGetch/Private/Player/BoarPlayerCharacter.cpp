// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerCharacter.h"

#include "Boar/BoarBase.h"
#include "Core/BoarGameMode.h"
#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Component/CaptureComponent.h"
#include "Interaction/Interactable.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

ABoarPlayerCharacter::ABoarPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	//-------------------------------------------------
	// Components
	//-------------------------------------------------
	
	// 各機能を担当するActorComponentを生成
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

	// プレイヤー追従カメラ
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	
	// CharacterMovementの初期設定
	GetCharacterMovement()->RotationRate = FRotator(0.f, RotationRate, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->JumpZVelocity = JumpVelocity;
	// Characterの標準ジャンプ回数設定を使って二段ジャンプに対応する。
	JumpMaxCount = FMath::Max(1, MaxJumpCountSetting);
	GetCharacterMovement()->AirControl = AirControl;
}

void ABoarPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// プレイヤー被弾判定はカプセル接触で受ける。
		Capsule->OnComponentBeginOverlap.AddDynamic(this, &ABoarPlayerCharacter::OnCapsuleBeginOverlap);
	}

	if (HealthComponent)
	{
		// HP0時の遷移はGameModeに一本化する。
		HealthComponent->OnDeath.AddDynamic(this, &ABoarPlayerCharacter::OnDeath);
	}
}

void ABoarPlayerCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (bIsStunned || CurrentActionState == EPlayerActionState::Capture)
	{
		return;
	}

	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		SetPlayerActionState(JumpCurrentCount >= 2 ? EPlayerActionState::DoubleJump : EPlayerActionState::Fall);
		return;
	}

	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::Move(const FVector2D& Input)
{
	if (Controller == nullptr || IsActionLocked()) return;

	// カメラの向きを基準に移動方向を計算
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

	const bool bHasMoveInput = !FMath::IsNearlyZero(Input.X) || !FMath::IsNearlyZero(Input.Y);
	bHadMoveInput = bHasMoveInput;
	UpdateGroundActionState(bHasMoveInput);
}

// カメラを回転させる
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
	if (IsActionLocked()) return;
	
	Jump();
	SetPlayerActionState(JumpCurrentCount >= 2 ? EPlayerActionState::DoubleJump : EPlayerActionState::Jump);
}

void ABoarPlayerCharacter::StopJump()
{
	StopJumping();
}

// 現在選択中のガジェットを使用する
void ABoarPlayerCharacter::UseGadget()
{
	if (GadgetComponent && !IsActionLocked())
	{
		SetPlayerActionState(EPlayerActionState::UseGadget);
		GadgetComponent->UseCurrentGadget();
		UpdateGroundActionState(bHadMoveInput);
	}
}

// ガジェットスロットを切り替える
void ABoarPlayerCharacter::SwitchGadgetSlot(int32 SlotIndex)
{
	if (GadgetComponent && !IsActionLocked())
	{
		GadgetComponent->SwitchGadgetBySlot(SlotIndex);
	}
}

// インタラクト可能なオブジェクトを探して実行する
void ABoarPlayerCharacter::Interact()
{
	if (IsActionLocked())	return;
	
	UWorld* World = GetWorld();
	if (World == nullptr)	return;

	SetPlayerActionState(EPlayerActionState::Interact);
	
	// カメラ位置を始点、カメラの前方向を判定方向とする
	const FVector Start = FollowCamera ? FollowCamera->GetComponentLocation() : GetActorLocation();
	const FVector Direction = FollowCamera ? FollowCamera->GetForwardVector() : GetActorForwardVector();
	const FVector End = Start + Direction * InteractDistance;

	// 自分自身には当たらないようにする
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// 前方へSphereSweepを行い、Interact対象を検索する
	FHitResult HitResult;
	const bool bHit = World->SweepSingleByChannel(
		HitResult,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(InteractRadius),
		QueryParams
	);

	if (!bHit)
	{
		UpdateGroundActionState(bHadMoveInput);
		return;
	}
	
	// Interactableインターフェースを実装しているActorならInteractを呼び出す
	AActor* HitActor = HitResult.GetActor();
	if (HitActor && HitActor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
	{
		IInteractable::Execute_Interact(HitActor, this);
	}

	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::Capture()
{
	if (CaptureComponent && !IsActionLocked())
	{
		SetPlayerActionState(EPlayerActionState::Capture);
		CaptureComponent->Capture(this);
		UpdateGroundActionState(bHadMoveInput);
	}
}

void ABoarPlayerCharacter::StartDash()
{
	if (IsActionLocked()) return;
	
	bIsDashing = true;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = DashSpeed;
	}
	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::StopDash()
{
	bIsDashing = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = WalkSpeed;
	}
	UpdateGroundActionState(bHadMoveInput);
}

// イノシシとの接触時に呼ばれる
void ABoarPlayerCharacter::OnCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	if (bIsInvincible || bIsStunned || HealthComponent == nullptr || OtherActor == nullptr || OtherActor == this)
		return;
	

	if (Cast<ABoarBase>(OtherActor) == nullptr)
		return;
	
	// ダメージを受けてスタン状態にする
	HealthComponent->TakeDamage(ContactDamage);
	BeginStun();

	// 多段ヒットを防ぐため、被弾後に短い無敵を付与する。
	bIsInvincible = true;
	GetWorldTimerManager().ClearTimer(InvincibleTimerHandle);
	GetWorldTimerManager().SetTimer(InvincibleTimerHandle, this, &ABoarPlayerCharacter::EndInvincible, InvincibleDuration, false);
}

void ABoarPlayerCharacter::OnDeath()
{
	if (ABoarGameMode* GameMode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
	{
		GameMode->HandlePlayerDeath(this);
	}
}

void ABoarPlayerCharacter::BeginStun()
{
	bIsStunned = true;
	SetPlayerActionState(EPlayerActionState::Stun);
	StopDash();
	
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// スタン中は移動入力を受けても動かないようにする。
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}

	GetWorldTimerManager().ClearTimer(StunTimerHandle);
	GetWorldTimerManager().SetTimer(StunTimerHandle, this, &ABoarPlayerCharacter::EndStun, StunDuration, false);
}

void ABoarPlayerCharacter::EndStun()
{
	bIsStunned = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetMovementMode(MOVE_Walking);
		Move->MaxWalkSpeed = bIsDashing ? DashSpeed : WalkSpeed;
	}
	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::EndInvincible()
{
	bIsInvincible = false;
}

void ABoarPlayerCharacter::UpdateGroundActionState(bool bHasMoveInput)
{
	if (IsActionLocked())
	{
		return;
	}

	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		return;
	}

	if (!bHasMoveInput)
	{
		SetPlayerActionState(EPlayerActionState::Idle);
		return;
	}

	SetPlayerActionState(bIsDashing ? EPlayerActionState::Run : EPlayerActionState::Walk);
}

void ABoarPlayerCharacter::SetPlayerActionState(EPlayerActionState NewState)
{
	CurrentActionState = NewState;
}

bool ABoarPlayerCharacter::IsActionLocked() const
{
	return bIsStunned;
}