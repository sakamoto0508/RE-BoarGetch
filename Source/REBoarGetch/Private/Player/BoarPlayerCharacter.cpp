// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerCharacter.h"

#include "Boar/BoarBase.h"
#include "Core/BoarGameMode.h"
#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Component/CaptureComponent.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Gadget/NetGadget.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Engine/World.h"

ABoarPlayerCharacter::ABoarPlayerCharacter()
{
	// Tickは空中アミ捕獲中の急降下制御だけに使う（非実行時は即return）。
	PrimaryActorTick.bCanEverTick = true;

	// 空中アミ捕獲のAnimation既定値。BP_PlayerCharacterのClass Defaultsで差し替え可能。
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> AirAnticipation(
		TEXT("/Game/Animation/Player/AirNetDrop/A_Player_AirNetDrop_Anticipation"));
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> AirSwing(
		TEXT("/Game/Animation/Player/AirNetDrop/A_Player_AirNetDrop_Swing"));
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> AirLoop(
		TEXT("/Game/Animation/Player/AirNetDrop/A_Player_AirNetDrop_DropLoop"));
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> AirRecovery(
		TEXT("/Game/Animation/Player/AirNetDrop/A_Player_AirNetDrop_Recovery"));
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> AirLand(
		TEXT("/Game/Animation/Player/AirNetDrop/A_Player_AirNetDrop_Land"));
	AirNetDropAnticipationAnimation = AirAnticipation.Object;
	AirNetDropSwingAnimation = AirSwing.Object;
	AirNetDropLoopAnimation = AirLoop.Object;
	AirNetDropRecoveryAnimation = AirRecovery.Object;
	AirNetDropLandAnimation = AirLand.Object;

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

	// 接触判定のイベント登録。
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// プレイヤー被弾判定はカプセル接触で受ける。
		Capsule->OnComponentBeginOverlap.AddDynamic(this, &ABoarPlayerCharacter::OnCapsuleBeginOverlap);
		// Pawn同士がBlockする設定では接触はHitとして届くため、同じ被弾処理へ渡す。
		Capsule->OnComponentHit.AddDynamic(this, &ABoarPlayerCharacter::OnCapsuleHit);
	}

	if (HealthComponent)
	{
		// HP0時の遷移はGameModeに一本化する。
		HealthComponent->OnDeath.AddDynamic(this, &ABoarPlayerCharacter::OnDeath);
	}

	// Stages prepare the player before BeginPlay. Unmanaged lobby worlds and
	// runtime-spawned players use the same initialization with an explicit context.
	const auto* Stage = GetWorld()->GetAuthGameMode<ABoarGameMode>();
	if (!bStageInitialized && (!Stage || Stage->CanAdvanceStage()) &&
		!InitializeForPlay(BoarGadgetLoadoutInitializer::ResolveContext(GetWorld())))
	{
		GetCharacterMovement()->DisableMovement();
		UE_LOG(LogTemp, Error, TEXT("[WorldInit] Player preparation failed: %s"), *GetName());
	}
}

bool ABoarPlayerCharacter::InitializeForPlay(const FBoarGadgetLoadoutContext& Context)
{
	if (bStageInitialized) return true;
	if (!HealthComponent || !HealthComponent->InitializeHealth()
		|| !BoarGadgetLoadoutInitializer::Initialize(GadgetComponent, Context)) return false;
	bStageInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("[StageInit] Player ready: %s"), *GetName());
	return true;
}

void ABoarPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (AirNetDropPhase != EAirNetDropPhase::None)
	{
		TickAirNetDrop(DeltaSeconds);
	}
}

void ABoarPlayerCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	// 空中アミ捕獲中はUseGadget状態を上書きせず、着地だけを専用処理へ渡す。
	if (AirNetDropPhase != EAirNetDropPhase::None)
	{
		const UCharacterMovementComponent* Move = GetCharacterMovement();
		if (Move && Move->IsMovingOnGround())
		{
			HandleAirNetDropLanded();
		}
		else if (Move && !Move->IsFalling())
		{
			// 地上・空中以外（移動無効化など）へ移った場合は継続させない。
			EndGadgetUse(true);
		}
		return;
	}

	if (bIsStunned || bIsDowned || CurrentActionState == EPlayerActionState::Capture)
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

	// 空中アミ捕獲の空中段階だけは、倍率を掛けたAirControlで操作を受け付ける（倍率0なら従来どおり入力無効）。
	const bool bAirNetDropSteering = AirNetDropPhase != EAirNetDropPhase::None &&
		AirNetDropPhase != EAirNetDropPhase::Landing && AirNetDropHorizontalControlMultiplier > 0.0f &&
		!bIsDowned;
	if (IsActionLocked() && !bAirNetDropSteering)
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

		// 空中かどうかは使用開始のこの時点で確定させ、途中で上昇→下降に変わっても地上用へ切り替えない。
		const bool bAirNetDrop = ShouldStartAirNetDrop(CurrentGadget);
		// 空中のアミはCooldown中なら使わない（地上用Montageと移動停止で空中に静止してしまうのを防ぐ）。
		if (bAirNetDrop && !CurrentGadget->CanUse())
			return;

		// 使用開始後にガジェットが切り替わっても判定できるよう、UseStyleをCharacter側へ保存する。
		CurrentGadgetUseStyle = CurrentGadget->GetUseStyle();
		if (!GadgetComponent->BeginUseCurrentGadget())
			return;

		bIsGadgetInUse = true;

		if (bAirNetDrop)
		{
			// 空中アミは鉛直速度を残したまま急降下へ移るため、StopMovementImmediatelyを通さない。
			if (UCharacterMovementComponent* Move = GetCharacterMovement())
			{
				Move->bOrientRotationToMovement = false;
			}
		}
		else if (UCharacterMovementComponent* Move = GetCharacterMovement();
				Move && !CurrentGadget->CanMoveWhileUsing())
		{
				// ガジェット使用中は移動入力を受けても動かないようにする。
				Move->StopMovementImmediately();
				// 移動方向へキャラクターを自動回転させる機能をオフ。
				Move->bOrientRotationToMovement = false;
		}

		bHadMoveInput = false;

		SetPlayerActionState(EPlayerActionState::UseGadget);
		if (bAirNetDrop)
		{
			// 空中用AnimationはC++側でFullBodyスロットへ再生する（BPの地上用Montageは再生しない）。
			if (!BeginAirNetDrop())
			{
				UE_LOG(LogTemp, Warning, TEXT("[AirNetDrop] Animation could not be played; ending gadget use"));
				EndGadgetUse(true);
			}
			return;
		}
		//BPイベントでガジェット使用開始を通知する。
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
	HandleBoarContact(OtherActor);
}

void ABoarPlayerCharacter::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                                        FVector NormalImpulse, const FHitResult& Hit)
{
	(void)HitComponent;
	(void)OtherComp;
	(void)NormalImpulse;
	(void)Hit;
	HandleBoarContact(OtherActor);
}

void ABoarPlayerCharacter::HandleBoarContact(AActor* OtherActor)
{
	// 無敵中・スタン中・ダウン中は追加ダメージを受けないComponentや接触相手が無効な場合も処理しない。
	if (bIsInvincible || bIsStunned || bIsDowned || HealthComponent == nullptr || OtherActor == nullptr || OtherActor == this)
		return;

	if (Cast<ABoarBase>(OtherActor) == nullptr)
		return;
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage()) return;

	// ダメージを受けてスタン状態にする
	HealthComponent->TakeDamage(ContactDamage);
	// HP0ではStunを開始せず、GameOver演出のダウンアニメがStun状態で上書きされないようにする。
	if (HealthComponent->IsDead())
	{
		EnterDowned();
		return;
	}
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
	EnterDowned();
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

void ABoarPlayerCharacter::EnterDowned()
{
	if (bIsDowned) return;
	bIsDowned = true;
	// 既に開始していたStunのTimerで通常状態へ戻らないようにする。
	GetWorldTimerManager().ClearTimer(StunTimerHandle);
	bIsStunned = false;
	EndGadgetUse(true);
	StopDash();
	if (UCharacterMovementComponent* Move = GetCharacterMovement()) Move->StopMovementImmediately();
	SetPlayerActionState(EPlayerActionState::Down);
}

void ABoarPlayerCharacter::EndStun()
{
	bIsStunned = false;
	if (bIsDowned) return;
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage()) return;
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

	// 空中アミ捕獲の急降下・移動値・Montageをどの終了経路でも必ず元に戻す。
	const bool bWasAirNetDrop = AirNetDropPhase != EAirNetDropPhase::None;
	EndAirNetDrop();

	if (GadgetComponent)
	{
		GadgetComponent->EndUseCurrentGadget();
	}

	bIsGadgetInUse = false;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
	}

	// AnimBPへ通常終了または中断終了したことを通知する。
	OnGadgetUseStopped(CurrentGadgetUseStyle, CurrentGadget);

	if (bWasInterrupted)
	{
		OnGadgetUseInterrupted();
	}

	// 空中で引き戻しまで終えた場合は、UseGadgetのまま落下しないよう通常のFallへ戻す。
	if (bWasAirNetDrop && !bIsStunned && !bIsDowned && GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		SetPlayerActionState(EPlayerActionState::Fall);
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
	if (const ABoarGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
		if (!Mode->CanAdvanceStage()) return true;
	return bIsStunned || bIsGadgetInUse || bIsDowned;
}

//-------------------------------------------------
// Air Net Drop（空中アミ捕獲）
//-------------------------------------------------

ANetGadget* ABoarPlayerCharacter::GetEquippedNetGadget() const
{
	return GadgetComponent ? Cast<ANetGadget>(GadgetComponent->GetCurrentGadget()) : nullptr;
}

bool ABoarPlayerCharacter::ShouldStartAirNetDrop(const AGadgetBase* Gadget) const
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	// Jump上昇中・二段ジャンプ中・落下中はいずれもFalling。
	return Cast<ANetGadget>(Gadget) && Move && Move->IsFalling() && AirNetDropAnticipationAnimation &&
		AirNetDropSwingAnimation && AirNetDropLoopAnimation && AirNetDropRecoveryAnimation && AirNetDropLandAnimation &&
		GetMesh() && GetMesh()->GetAnimInstance();
}

bool ABoarPlayerCharacter::BeginAirNetDrop()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Move == nullptr)
		return false;

	// 押しっぱなしのジャンプ入力で上昇が再加算されないようにする（ジャンプ回数は変更しない）。
	StopJumping();

	SavedAirNetDropGravityScale = Move->GravityScale;
	SavedAirNetDropAirControl = Move->AirControl;
	bAirNetDropMovementOverridden = true;
	Move->AirControl = SavedAirNetDropAirControl * AirNetDropHorizontalControlMultiplier;
	// 予備動作：上昇を止め、重力を弱めて一瞬ためる。
	Move->Velocity.Z = FMath::Min(Move->Velocity.Z, AirNetDropAnticipationMaxRiseSpeed);
	Move->GravityScale = SavedAirNetDropGravityScale * AirNetDropAnticipationGravityMultiplier;

	AirNetDropDiveElapsed = 0.0f;
	return EnterAirNetDropPhase(EAirNetDropPhase::Anticipation);
}

void ABoarPlayerCharacter::TickAirNetDrop(float DeltaSeconds)
{
	AirNetDropPhaseElapsed += DeltaSeconds;
	UCharacterMovementComponent* Move = GetCharacterMovement();

	// 網が1フレームで大きく動くため、判定窓が開いている間は移動量を掃引して取りこぼしを防ぐ。
	if (ANetGadget* NetGadget = GetEquippedNetGadget())
	{
		NetGadget->SweepCaptureWindow();
	}

	if (Move == nullptr || !Move->IsFalling())
		return;

	switch (AirNetDropPhase)
	{
	case EAirNetDropPhase::Anticipation:
	{
		Move->Velocity.Z = FMath::Min(Move->Velocity.Z, AirNetDropAnticipationMaxRiseSpeed);
		// 次のAnimationへクロスフェードできるよう、ブレンド時間ぶん早めに切り替える。
		if (AirNetDropPhaseElapsed >= FMath::Max(0.0f, AirNetDropAnticipationDuration - AirNetDropBlendTime))
		{
			BeginAirNetDropDive();
			if (!EnterAirNetDropPhase(EAirNetDropPhase::Swing)) EndGadgetUse(true);
		}
		break;
	}
	case EAirNetDropPhase::Swing:
	case EAirNetDropPhase::Drop:
	{
		AirNetDropDiveElapsed += DeltaSeconds;
		// 急降下：通常重力の代わりに設定加速度で加速し、最大落下速度で止める。
		Move->Velocity.Z = FMath::Max(Move->Velocity.Z - AirNetDropDownAcceleration * DeltaSeconds, -AirNetDropMaxFallSpeed);

		if (AirNetDropPhase == EAirNetDropPhase::Swing)
		{
			const float SwingDuration = AirNetDropSwingAnimation->GetPlayLength() / FMath::Max(0.1f, AirNetDropSwingPlayRate);
			if (AirNetDropPhaseElapsed >= FMath::Max(0.0f, SwingDuration - AirNetDropBlendTime))
			{
				if (!EnterAirNetDropPhase(EAirNetDropPhase::Drop)) EndGadgetUse(true);
			}
		}
		else if (AirNetDropDiveElapsed >= AirNetDropMaxDiveDuration)
		{
			// 高所では急降下を打ち切り、引き戻してから通常の重力落下へ戻す。
			RestoreAirNetDropMovement();
			if (!EnterAirNetDropPhase(EAirNetDropPhase::Recovery)) EndGadgetUse(true);
		}
		break;
	}
	default:
		break;
	}
}

void ABoarPlayerCharacter::BeginAirNetDropDive()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Move == nullptr)
		return;

	// 急降下中の加速は設定値で管理するため、通常重力は止める（終了時にRestoreで戻す）。
	Move->GravityScale = 0.0f;
	Move->Velocity.Z = FMath::Min(Move->Velocity.Z, -AirNetDropInitialDownSpeed);
	Move->Velocity.X *= AirNetDropHorizontalVelocityScale;
	Move->Velocity.Y *= AirNetDropHorizontalVelocityScale;
	AirNetDropDiveElapsed = 0.0f;
}

bool ABoarPlayerCharacter::EnterAirNetDropPhase(EAirNetDropPhase NewPhase)
{
	AirNetDropPhase = NewPhase;
	AirNetDropPhaseElapsed = 0.0f;

	switch (NewPhase)
	{
	case EAirNetDropPhase::Anticipation:
	{
		// 予備動作Animationは設定秒数に合わせて再生速度を変える。
		const float Length = AirNetDropAnticipationAnimation->GetPlayLength();
		return PlayAirNetDropAnimation(AirNetDropAnticipationAnimation, Length / FMath::Max(0.02f, AirNetDropAnticipationDuration), 1, 0.0f);
	}
	case EAirNetDropPhase::Swing:
		return PlayAirNetDropAnimation(AirNetDropSwingAnimation, AirNetDropSwingPlayRate, 1, 0.0f);
	case EAirNetDropPhase::Drop:
		// 着地か最大急降下時間まで構えを保持する（ループ回数は十分大きく取る）。
		return PlayAirNetDropAnimation(AirNetDropLoopAnimation, 1.0f, 1000, 0.0f);
	case EAirNetDropPhase::Recovery:
		return PlayAirNetDropAnimation(AirNetDropRecoveryAnimation, 1.0f, 1, 0.0f);
	default:
		return false;
	}
}

void ABoarPlayerCharacter::HandleAirNetDropLanded()
{
	if (AirNetDropPhase == EAirNetDropPhase::None || AirNetDropPhase == EAirNetDropPhase::Landing)
		return;

	// 着地後に急降下の速度制御が残らないよう、先に移動値を戻す。
	RestoreAirNetDropMovement();

	// 振り下ろし前・振り下ろし中・急降下中の着地はアミを地面へ叩き付ける（冒頭のNotifyで判定窓ON→OFF）。
	// 引き戻し中は判定窓が閉じているため、叩き付けNotifyより後ろから再生して再び開かない。
	const bool bSlam = AirNetDropPhase != EAirNetDropPhase::Recovery;
	if (!bSlam)
	{
		if (ANetGadget* NetGadget = GetEquippedNetGadget())
		{
			NetGadget->EndCaptureWindow();
		}
	}

	AirNetDropPhase = EAirNetDropPhase::Landing;
	AirNetDropPhaseElapsed = 0.0f;
	const float Length = AirNetDropLandAnimation->GetPlayLength();
	const float StartTime = bSlam ? 0.0f : FMath::Min(AirNetDropLandingSkipCaptureTime, Length);
	const float PlayRate = Length / FMath::Max(0.05f, AirNetDropLandingRecoveryDuration);
	if (!PlayAirNetDropAnimation(AirNetDropLandAnimation, PlayRate, 1, StartTime))
	{
		EndGadgetUse(true);
	}
}

void ABoarPlayerCharacter::RestoreAirNetDropMovement()
{
	if (!bAirNetDropMovementOverridden)
		return;

	bAirNetDropMovementOverridden = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = SavedAirNetDropGravityScale;
		Move->AirControl = SavedAirNetDropAirControl;
	}
}

void ABoarPlayerCharacter::EndAirNetDrop()
{
	RestoreAirNetDropMovement();
	if (AirNetDropPhase == EAirNetDropPhase::None)
		return;

	AirNetDropPhase = EAirNetDropPhase::None;
	AirNetDropPhaseElapsed = 0.0f;
	AirNetDropDiveElapsed = 0.0f;

	// 中断時（被弾・Stage終了など）は再生中のAnimationを止める。完了済みなら何もしない。
	UAnimMontage* Montage = AirNetDropMontage;
	ClearAirNetDropMontageDelegate();
	AirNetDropMontage = nullptr;
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance && Montage && AnimInstance->Montage_IsPlaying(Montage))
	{
		AnimInstance->Montage_Stop(AirNetDropBlendTime, Montage);
	}
}

bool ABoarPlayerCharacter::PlayAirNetDropAnimation(UAnimSequenceBase* Animation, float PlayRate, int32 LoopCount, float StartTime)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance == nullptr || Animation == nullptr)
		return false;

	// 前段階のMontageは次の再生で割り込み終了するため、中断扱いにならないよう先に通知を外す。
	ClearAirNetDropMontageDelegate();
	UAnimMontage* Montage = AnimInstance->PlaySlotAnimationAsDynamicMontage(Animation, AirNetDropSlotName,
		AirNetDropBlendTime, AirNetDropBlendTime, PlayRate, LoopCount, -1.0f, StartTime);
	AirNetDropMontage = Montage;
	if (Montage == nullptr)
		return false;

	FOnMontageEnded EndedDelegate;
	EndedDelegate.BindUObject(this, &ABoarPlayerCharacter::OnAirNetDropMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndedDelegate, Montage);
	return true;
}

void ABoarPlayerCharacter::ClearAirNetDropMontageDelegate()
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	// ブレンドアウト中のインスタンスにも届くよう、再生中かどうかに関わらず外す。
	if (AnimInstance && AirNetDropMontage)
	{
		FOnMontageEnded Unbound;
		AnimInstance->Montage_SetEndDelegate(Unbound, AirNetDropMontage);
	}
}

void ABoarPlayerCharacter::OnAirNetDropMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != AirNetDropMontage || AirNetDropPhase == EAirNetDropPhase::None)
		return;

	AirNetDropMontage = nullptr;
	if (bInterrupted)
	{
		// GameOver・Clear演出など外部のMontageに割り込まれた場合は中断終了する。
		EndGadgetUse(true);
		return;
	}

	switch (AirNetDropPhase)
	{
	case EAirNetDropPhase::Anticipation:
		// 通常はTick側が先に切り替えるが、低FPSでAnimationが先に終わった場合もここで進める。
		BeginAirNetDropDive();
		if (!EnterAirNetDropPhase(EAirNetDropPhase::Swing)) EndGadgetUse(true);
		break;
	case EAirNetDropPhase::Swing:
		if (!EnterAirNetDropPhase(EAirNetDropPhase::Drop)) EndGadgetUse(true);
		break;
	case EAirNetDropPhase::Drop:
		RestoreAirNetDropMovement();
		if (!EnterAirNetDropPhase(EAirNetDropPhase::Recovery)) EndGadgetUse(true);
		break;
	default:
		// 引き戻し・着地の完了で空中アミ捕獲を終える。
		FinishGadgetUseAnimation(false);
		break;
	}
}
