// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerCharacter.h"

#include "Boar/BoarBase.h"
#include "Core/BoarGameMode.h"
#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Component/CaptureComponent.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Gadget/NetGadget.h"
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
		return;


	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		SetPlayerActionState(JumpCurrentCount >= 2 ? EPlayerActionState::DoubleJump : EPlayerActionState::Fall);
		return;
	}

	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::Move(const FVector2D& Input)
{
	if (Controller == nullptr || bIsStunned)
		return;

	// カメラのYaw回転だけを使用し、上下のカメラ角度によって移動方向が傾かないようにする。
	const FRotator ControlRot = Controller->GetControlRotation();
	const FRotator YawRot(0.f, ControlRot.Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
	const FVector DesiredDirection = Forward * Input.Y + Right * Input.X;

	// ガジェット使用中は位置を動かさず、移動入力の方向へ向きだけを変更する。
	if (bIsGadgetInUse)
	{
		if (!DesiredDirection.IsNearlyZero())
		{
			SetActorRotation(DesiredDirection.Rotation());
		}
		bHadMoveInput = false;
		return;
	}

	if (IsActionLocked())
		return;

	if (!FMath::IsNearlyZero(Input.Y))
	{
		AddMovementInput(Forward, Input.Y);
	}
	if (!FMath::IsNearlyZero(Input.X))
	{
		AddMovementInput(Right, Input.X);
	}

	// スタン終了時や着地時にも正しい移動状態へ戻せるよう、最後に移動入力があったかを保存しておく。
	const bool bHasMoveInput = !FMath::IsNearlyZero(Input.X) || !FMath::IsNearlyZero(Input.Y);
	bHadMoveInput = bHasMoveInput;
	// 地上にいる場合は入力内容に応じて切り替える。
	UpdateGroundActionState(bHasMoveInput);
}

// カメラを回転させる
void ABoarPlayerCharacter::Look(const FVector2D& Input)
{
	if (Controller == nullptr)
		return;

	// ControllerのYawを変更し、SpringArmをプレイヤーの周囲に回転させる。
	if (!FMath::IsNearlyZero(Input.X))
	{
		AddControllerYawInput(Input.X);
	}

	// ControllerのPitchを変更し、カメラを上下方向へ回転させる。
	if (!FMath::IsNearlyZero(Input.Y))
	{
		AddControllerPitchInput(Input.Y);
	}

	// ガジェット使用中はカメラのYawにキャラクターの向きも追従させる。
	if (bIsGadgetInUse)
	{
		const FRotator ControlRotation = Controller->GetControlRotation();
		SetActorRotation(FRotator(0.0f, ControlRotation.Yaw, 0.0f));
	}
}

void ABoarPlayerCharacter::StartJump()
{
	if (IsActionLocked()) return;
	if (!CanJump()) return;
	
	// 地上ジャンプ前は0、二段ジャンプ前は1
	const bool bIsDoubleJump = GetCharacterMovement() &&
		GetCharacterMovement()->IsFalling() &&
		JumpCurrentCount >= 1;

	Jump();

	SetPlayerActionState(bIsDoubleJump
			? EPlayerActionState::DoubleJump
			: EPlayerActionState::Jump);
}

void ABoarPlayerCharacter::StopJump()
{
	StopJumping();
}

// ガジェット使用を開始する
void ABoarPlayerCharacter::StartGadgetUse()
{
	if (GadgetComponent && !IsActionLocked())
	{
		if (bIsGadgetInUse)
			return;

		AGadgetBase* CurrentGadget = GadgetComponent->GetCurrentGadget();
		if (CurrentGadget == nullptr)
			return;

		// 使用開始後にガジェットが切り替わっても判定できるよう、UseStyleをCharacter側へ保存する。
		CurrentGadgetUseStyle = CurrentGadget->GetUseStyle();
		if (!GadgetComponent->BeginUseCurrentGadget())
			return;

		bIsGadgetInUse = true;
		SetPlayerActionState(EPlayerActionState::UseGadget);
		OnGadgetUseStarted(CurrentGadgetUseStyle, CurrentGadget);

		UpdateGroundActionState(bHadMoveInput);
	}
}

// ガジェット使用を終了する
void ABoarPlayerCharacter::StopGadgetUse()
{
	if (!bIsGadgetInUse)
		return;

	// OneShotは入力を離した時ではなく、Montageの完了時に終了する。
	if (CurrentGadgetUseStyle == EGadgetUseStyle::OneShot)
		return;

	// Holdなどの継続使用形式は、入力を離した時点で使用を終了する。
	EndGadgetUse(false);
}

void ABoarPlayerCharacter::FinishGadgetUseAnimation(bool bWasInterrupted)
{
	EndGadgetUse(bWasInterrupted);
}

// ガジェットスロットを切り替える
void ABoarPlayerCharacter::SwitchGadgetSlot(int32 SlotIndex)
{
	if (GadgetComponent == nullptr || IsActionLocked())
		return;
	
	if (bIsGadgetInUse)
	{
		// 継続使用形式のガジェットを使用中に装備変更する場合 前のガジェットの終了処理を先に行う。
		StopGadgetUse();
		// OneShot形式の場合、StopGadgetUseは終了を行わないため、現在のIsActionLockedの条件では基本的にここには到達しない。
	}

	GadgetComponent->SwitchGadgetBySlot(SlotIndex);
}

void ABoarPlayerCharacter::Capture()
{
	if (CaptureComponent == nullptr || IsActionLocked())
		return;

	SetPlayerActionState(EPlayerActionState::Capture);
	// 捕獲対象の検索や捕獲成立判定はCaptureComponent側へ任せる。
	CaptureComponent->Capture(this);
	// Captureが同期的に完了する仕様の場合は、捕獲終了後の地上状態へ戻す。
	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::StartDash()
{
	if (IsActionLocked()) 
		return;

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
                                                 UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                                 const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	// 無敵中・スタン中は追加ダメージを受けないComponentや接触相手が無効な場合も処理しない。
	if (bIsInvincible || bIsStunned || HealthComponent == nullptr || OtherActor == nullptr || OtherActor == this)
		return;
	
	if (Cast<ABoarBase>(OtherActor) == nullptr)
		return;

	// ダメージを受けてスタン状態にする
	HealthComponent->TakeDamage(ContactDamage);
	BeginStun();

	// 多段ヒットを防ぐため、被弾後に短い無敵を付与する。
	bIsInvincible = true;
	// 以前の無敵終了Timerが残っている場合は、新しい被弾時間を基準に再設定する。
	GetWorldTimerManager().ClearTimer(InvincibleTimerHandle);
	GetWorldTimerManager().SetTimer(InvincibleTimerHandle, this, &ABoarPlayerCharacter::EndInvincible,
	                                InvincibleDuration, false);
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
	// ガジェット使用中に被弾した場合はMontageや捕獲判定を中断扱いで終了する。
	EndGadgetUse(true);
	StopDash();

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// スタン中は移動入力を受けても動かないようにする。
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}

	// 連続してスタン処理が呼ばれた場合に備え、以前のTimerを破棄してから新しく設定する。
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

void ABoarPlayerCharacter::EndGadgetUse(bool bWasInterrupted)
{
	// Montage停止後にInterruptedコールバックが重複しても、一度だけ終了する。
	if (!bIsGadgetInUse)
		return;

	AGadgetBase* CurrentGadget =
		GadgetComponent ? GadgetComponent->GetCurrentGadget() : nullptr;

	// Notify OFFに到達しない中断時も、捕獲判定を必ず閉じる。
	if (ANetGadget* NetGadget = Cast<ANetGadget>(CurrentGadget))
	{
		NetGadget->EndCaptureWindow();
	}

	if (GadgetComponent)
	{
		GadgetComponent->EndUseCurrentGadget();
	}

	bIsGadgetInUse = false;
	// AnimBPへ通常終了または中断終了したことを通知する。
	OnGadgetUseStopped(CurrentGadgetUseStyle, CurrentGadget);

	if (bWasInterrupted)
	{
		OnGadgetUseInterrupted();
	}

	// ガジェットによる入力ロックが解除されたため、現在の移動入力からIdle・Walk・Runへ復帰させる。
	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::UpdateGroundActionState(bool bHasMoveInput)
{
	if (IsActionLocked())
		return;

	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
		return;
	
	if (!bHasMoveInput)
	{
		SetPlayerActionState(EPlayerActionState::Idle);
		return;
	}

	// 地上で移動入力がある場合は、ダッシュ状態に応じてWalkまたはRunを設定する。
	SetPlayerActionState(bIsDashing ? EPlayerActionState::Run : EPlayerActionState::Walk);
}

void ABoarPlayerCharacter::SetPlayerActionState(EPlayerActionState NewState)
{
	CurrentActionState = NewState;
}

bool ABoarPlayerCharacter::IsActionLocked() const
{
	return bIsStunned || bIsGadgetInUse;
}
