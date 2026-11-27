#include "Player/BoarPlayerCharacter.h"
#include "REBoarGetch.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Engine/GameInstance.h"

#include "Boar/BoarBase.h"
#include "Core/BoarGameMode.h"
#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Component/CaptureComponent.h"
#include "Component/PlayerHitReactionComponent.h"
#include "Component/FootstepDustComponent.h"

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

namespace BoarPlayer
{
	/** Capsuleが無いときに使う大きさ（UEの標準的なCharacterと同じ、cm）。 */
	constexpr float FallbackCapsuleRadius = 34.0f;
	constexpr float FallbackCapsuleHalfHeight = 88.0f;

	/** 空中アミの急降下ループのループ回数。着地でLandingへ切り替えるまで回し続けるための、実質無限の値。 */
	constexpr int32 AirNetDropLoopCount = 1000;
}

ABoarPlayerCharacter::ABoarPlayerCharacter()
{
	// Tickはダッシュスタミナの消費・回復と、空中アミ捕獲中の急降下制御に使う。
	// Pause中はTickしない（bTickEvenWhenPaused=false）ため、回復待ち時間も進まない。
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

	// 被弾・Stunの演出はGameplay結果のEventを受けて再生する（AKI-163）。
	HitReactionComponent = CreateDefaultSubobject<UPlayerHitReactionComponent>(TEXT("HitReactionComponent"));

	// 走行の土煙（演出のみ）。Walk（〜250cm/s）では出さず、Run（500）で軽く、Dash（900）でやや強く。
	FootstepDustComponent = CreateDefaultSubobject<UFootstepDustComponent>(TEXT("FootstepDustComponent"));
	FootstepDustComponent->MinSpeed = 250.0f;
	FootstepDustComponent->FullStrengthSpeed = 900.0f;
	FootstepDustComponent->MinStrength = FFootstepDustStrength(/*SpawnCount=*/3, /*SizeScale=*/0.40f, /*Lifetime=*/0.30f, /*KickSpeed=*/70.0f, /*KickUp=*/30.0f, /*StreakCount=*/0);
	FootstepDustComponent->FullStrength = FFootstepDustStrength(/*SpawnCount=*/6, /*SizeScale=*/0.62f, /*Lifetime=*/0.42f, /*KickSpeed=*/150.0f, /*KickUp=*/50.0f, /*StreakCount=*/0);
	FootstepDustComponent->Spread = 0.6f;

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

void ABoarPlayerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// BP側でMaxStaminaを上書きしていても、開始時は満タンから始める。
	// OnPossessがBeginPlayより先に来てもHUDが正しい初期値を受け取れるよう、ここで確定させる。
	CurrentStamina = MaxStamina;
	TimeSinceStaminaDrain = StaminaRegenDelay;
	bDashExhausted = false;
}

void ABoarPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 購読済みの表示側へ初期値を確実に届ける。
	BroadcastStaminaChanged(true);

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

	// ステージではBeginPlay前にGameModeがPlayerを準備済み。GameModeの管理外のLobbyや、
	// 実行中にSpawnされたPlayerは、ここで同じ初期化を明示的なContextで行う。
	if (!bStageInitialized && ABoarGameMode::IsStageProgressAllowed(this) &&
		!InitializeForPlay(BoarGadgetLoadoutInitializer::ResolveContext(GetWorld())))
	{
		GetCharacterMovement()->DisableMovement();
		UE_LOG(LogREBoarGetch, Error, TEXT("[WorldInit] Playerの準備に失敗しました: %s"), *GetName());
	}
}

bool ABoarPlayerCharacter::InitializeForPlay(const FBoarGadgetLoadoutContext& Context)
{
	if (bStageInitialized)
		return true;
	if (!HealthComponent || !HealthComponent->InitializeHealth()
		|| !BoarGadgetLoadoutInitializer::Initialize(GadgetComponent, Context))
		return false;

	bStageInitialized = true;
	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] Playerの準備完了: %s"), *GetName());
	return true;
}

void ABoarPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickStamina(DeltaSeconds);

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
	// 使用中の移動可否は使用開始時に保存したCanMoveWhileUsingに従う（アミ等は従来どおり拒否）。
	if (!CanUseMovementActions() && !bAirNetDropSteering)
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
	// 移動を許可する使用（Hold型レーダー等）で移動入力がある間は、移動方向への自動回転を優先して上書きしない。
	const bool bMovingDuringUse = bGadgetUseAllowsMovement && !GetLastMovementInputVector().IsNearlyZero();
	if (bIsGadgetInUse && !bMovingDuringUse)
	{
		const FRotator ControlRotation = Controller->GetControlRotation();
		SetActorRotation(FRotator(0.0f, ControlRotation.Yaw, 0.0f));
	}
}

void ABoarPlayerCharacter::StartJump()
{
	if (!CanUseMovementActions())
		return;
	if (!CanJump())
		return;

	// 地上ジャンプ前は0、二段ジャンプ前は1
	const bool bIsDoubleJump = GetCharacterMovement() &&
		GetCharacterMovement()->IsFalling() &&
		JumpCurrentCount >= 1;

	Jump();
	PlayPlayerSound(bIsDoubleJump ? PlayerAudio.DoubleJump.Get() : PlayerAudio.Jump.Get());

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

		// Notifyが捕獲窓を管理する現行仕様では、素材欠落時に別の判定を代用しない。
		// 使用開始・Cooldown・速度変更より前に拒否し、通常の空中移動をそのまま保つ。
		if (bAirNetDrop && !HasAirNetDropPresentation())
		{
			UE_LOG(LogREBoarGetch, Warning, TEXT("[AirNetDrop] 必要なアニメーションかAnimInstanceが無いため、移動とクールダウンを変えずに使用を却下しました。"));
			return;
		}

		// 使用開始後にガジェットが切り替わっても判定できるよう、UseStyleをCharacter側へ保存する。
		CurrentGadgetUseStyle = CurrentGadget->GetUseStyle();

		if (!GadgetComponent->BeginUseCurrentGadget())
			return;

		// 移動可否も使用開始時点で確定する。空中アミは専用の操舵だけを許可するため対象外。
		bGadgetUseAllowsMovement = !bAirNetDrop && CurrentGadget->CanMoveWhileUsing();
		bIsGadgetInUse = true;

		// 移動を止める使用（空中アミを含む）では、移動方向への自動回転を止める。
		RefreshOrientRotationToMovement();

		// 空中アミは鉛直速度を残したまま急降下へ移るため、StopMovementImmediatelyを通さない。
		if (UCharacterMovementComponent* Move = GetCharacterMovement();
				Move && !bAirNetDrop && !bGadgetUseAllowsMovement)
		{
				// ガジェット使用中は移動入力を受けても動かないようにする。
				Move->StopMovementImmediately();
		}

		// 移動を止める使用だけ入力記録を消す。移動可能な使用では現在の入力を維持する。
		if (!bGadgetUseAllowsMovement)
		{
			bHadMoveInput = false;
		}

		SetPlayerActionState(EPlayerActionState::UseGadget);

		if (bAirNetDrop)
		{
			// 空中用AnimationはC++側でFullBodyスロットへ再生する（BPの地上用Montageは再生しない）。
			if (!BeginAirNetDrop())
			{
				UE_LOG(LogREBoarGetch, Warning, TEXT("[AirNetDrop] アニメーションを再生できないため、ガジェットの使用を終了します"));
				EndGadgetUse(true);
			}

			return;
		}

		// 地上用MontageはBPが再生するため、呼び出し前後の再生インスタンスを比べて今回の使用分だけを記録する。
		// 既に再生中の無関係なMontageは記録せず、中断時にも止めない。
		UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* MontageBefore = AnimInstance ? AnimInstance->GetActiveMontageInstance() : nullptr;
		const int32 MontageIDBefore = MontageBefore ? MontageBefore->GetInstanceID() : INDEX_NONE;
		GadgetUseMontage = nullptr;
		GadgetUseMontageInstanceID = INDEX_NONE;

		//BPイベントでガジェット使用開始を通知する。
		OnGadgetUseStarted(CurrentGadgetUseStyle, CurrentGadget);

		const FAnimMontageInstance* MontageAfter = AnimInstance ? AnimInstance->GetActiveMontageInstance() : nullptr;
		if (bIsGadgetInUse && MontageAfter && MontageAfter->GetInstanceID() != MontageIDBefore)
		{
			GadgetUseMontage = MontageAfter->Montage;
			GadgetUseMontageInstanceID = MontageAfter->GetInstanceID();
		}

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
	// BPのPlayMontageはどのMontageの終了かを渡さない。今回の使用で開始したMontageがまだ再生中
	// （BlendOutも始まっていない）なら、届いたのは以前の使用の古い終了通知なので今回の使用を終えない。
	if (bIsGadgetInUse && GadgetUseMontageInstanceID != INDEX_NONE)
	{
		UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* Instance =
			AnimInstance ? AnimInstance->GetMontageInstanceForID(GadgetUseMontageInstanceID) : nullptr;
		if (Instance && Instance->IsActive())
		{
			UE_LOG(LogREBoarGetch, Verbose, TEXT("[Gadget] 古いガジェットMontageからの終了通知を無視しました"));
			return;
		}
	}

	EndGadgetUse(bWasInterrupted);
}

bool ABoarPlayerCharacter::CanOpenNetCaptureWindow(const ANetGadget* Gadget, int32 MontageInstanceID) const
{
	if (!IsValid(this) || !IsValid(Gadget) || GadgetComponent == nullptr)
		return false;
	// 中断済み（Stun・Downed・装備解除・Stage終了）の後に届いたNotifyでは開かない。
	if (!bIsGadgetInUse || bIsStunned || bIsDowned)
		return false;
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return false;
	// Notifyの対象と現在使用中のガジェットが同じ網であること。
	if (GadgetComponent->GetCurrentGadget() != Gadget)
		return false;
	// 地上アミは今回の使用で開始したMontageのNotifyだけを受け付ける（BlendOut中の古いインスタンスを除外）。
	// 空中アミは段階ごとにC++がMontageを差し替えるため、使用中・空中段階であることで判断する。
	if (AirNetDropPhase == EAirNetDropPhase::None && MontageInstanceID != INDEX_NONE &&
		GadgetUseMontageInstanceID != INDEX_NONE && MontageInstanceID != GadgetUseMontageInstanceID)
		return false;

	return true;
}

void ABoarPlayerCharacter::StopGadgetUseMontage()
{
	UAnimMontage* Montage = GadgetUseMontage;
	const int32 InstanceID = GadgetUseMontageInstanceID;

	// 停止で届くInterrupted通知が再入しても、記録が消えているため二重に停止しない。
	GadgetUseMontage = nullptr;
	GadgetUseMontageInstanceID = INDEX_NONE;

	if (Montage == nullptr || InstanceID == INDEX_NONE)
		return;

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	FAnimMontageInstance* Instance = AnimInstance ? AnimInstance->GetMontageInstanceForID(InstanceID) : nullptr;

	// 既にBlendOut中・終了済み、または別のMontageに置き換わっている場合は触らない。
	if (Instance == nullptr || !Instance->IsActive() || Instance->Montage != Montage)
		return;
	// Montage自身のBlendOut設定で止め、他のMontage・同じMontageの別インスタンスには影響させない。
	Instance->Stop(Montage->BlendOut);
}

// ガジェットスロットを切り替える
void ABoarPlayerCharacter::SwitchGadgetSlot(int32 SlotIndex)
{
	// Stun・Downed・Stage停止中、およびOneShot使用中（アミ等）は切り替えない。
	if (GadgetComponent == nullptr || !CanSwitchGadgetSlot())
		return;

	if (bIsGadgetInUse)
	{
		// 同じSlot・範囲外の指定では装備が変わらないため、Hold使用を終了させずに継続する。
		if (!GadgetComponent->WouldSwitchChangeGadget(SlotIndex))
			return;
		// CanSwitchGadgetSlotによりここへ来るのはHold使用中だけ。ボタンを離した時と同じ通常終了経路で
		// 演出停止・使用状態解除・既存のCooldown開始を一度だけ行ってから切り替える。
		// 後続のUnequipGadget→InterruptGadgetUseは使用中フラグが下りているため重複しない。
		EndGadgetUse(false);
	}

	if (!GadgetComponent->SwitchGadgetBySlot(SlotIndex))
	{
		// 装備に失敗した場合、GadgetComponentは旧Gadgetを装備したまま残す。
		// Hold使用は終了済みで入力ロックも残らず、旧Gadgetを再度押せば通常どおり使用できる。
		UE_LOG(LogREBoarGetch, Warning, TEXT("[Gadget] スロット %d への素早い切替に失敗したため、前のガジェットを装備したままにします"), SlotIndex);
	}
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
	if (!CanUseMovementActions())
		return;
	// Dash入力のStarted（押した瞬間）からだけ呼ばれる。スタミナ切れで拒否した入力は、
	// 押し続けても再試行されないため、回復後も押し直しが必要になる。
	if (!CanStartDash())
		return;

	bIsDashing = true;
	PlayPlayerSound(PlayerAudio.DashStart);
	ApplyGroundMoveSpeed();
	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::StopDash()
{
	bIsDashing = false;
	ApplyGroundMoveSpeed();
	UpdateGroundActionState(bHadMoveInput);
}

//-------------------------------------------------
// ---- ダッシュとスタミナ ----
//-------------------------------------------------

void ABoarPlayerCharacter::ApplyGroundMoveSpeed()
{
	// Player側でMaxWalkSpeedを書くのはDash状態の切替だけ（Stunは入力拒否とノックバック、Stage停止はDisableMovement、空中アミはGravity/AirControl）。
	// スタミナの回復処理からは呼ばないため、他の状態が設定した移動値を回復のたびに上書きしない。
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = bIsDashing ? DashSpeed : WalkSpeed;
	}
}

bool ABoarPlayerCharacter::CanStartDash() const
{
	// 自発的に止めた場合は残量があれば再使用でき、枯渇した場合だけしきい値まで禁止する。
	return !bDashExhausted && CurrentStamina > 0.0f;
}

bool ABoarPlayerCharacter::ShouldDrainDashStamina() const
{
	if (!bIsDashing || CurrentStamina <= 0.0f || bMenuOpen)
		return false;
	// Stun・Downed・Stage停止・移動不可のガジェット使用中は消費しない。
	if (!CanUseMovementActions())
		return false;

	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Move == nullptr || !Move->IsMovingOnGround())
		return false;

	// 移動入力の有無はCMCが入力から作るAccelerationで判断する（入力なしの慣性移動では0）。
	// IgnoreMoveInput中もAddMovementInputが無視されるため0になる。
	const FVector InputAccel2D(Move->GetCurrentAcceleration().X, Move->GetCurrentAcceleration().Y, 0.0f);

	if (InputAccel2D.IsNearlyZero())
		return false;

	// Walking中のVelocityは実際の移動量から求め直されるため、壁へ正面から押し付けている間はほぼ0になる。
	const FVector Velocity2D(GetVelocity().X, GetVelocity().Y, 0.0f);

	if (Velocity2D.SizeSquared() < FMath::Square(DashDrainMinSpeed))
		return false;

	// 入力と逆向きへ慣性で滑っているだけ（切り返し直後など）は、まだDash移動が成立していないとみなす。
	return FVector::DotProduct(Velocity2D, InputAccel2D) > 0.0f;
}

void ABoarPlayerCharacter::TickStamina(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
		return;

	if (ShouldDrainDashStamina())
	{
		// 消費・回復ともにDeltaSecondsで積算し、フレームレートに依存しない。
		CurrentStamina = FMath::Max(0.0f, CurrentStamina - DashDrainPerSecond * DeltaSeconds);
		TimeSinceStaminaDrain = 0.0f;

		if (CurrentStamina <= 0.0f)
		{
			EndDashByExhaustion();
		}
	}
	else
	{
		const UCharacterMovementComponent* Move = GetCharacterMovement();

		// 既存仕様ではDash状態のままジャンプすると空中でもDash速度の上限が残る。
		// 空中は消費しない代わりに、Dashの勢いを保っている間は回復待ちも進めない（Dashジャンプ連打で無償回復しない）。
		const bool bCarryingDashInAir = bIsDashing && Move && Move->IsFalling() &&
			FVector(GetVelocity().X, GetVelocity().Y, 0.0f).SizeSquared() > FMath::Square(WalkSpeed);
		if (bCarryingDashInAir)
		{
			TimeSinceStaminaDrain = 0.0f;
		}
		else
		{
			// 回復開始の瞬間がフレームの途中でも、遅延を超えた分の時間だけ回復させる。
			const float PrevElapsed = TimeSinceStaminaDrain;
			TimeSinceStaminaDrain = PrevElapsed + DeltaSeconds;
			const float RegenSeconds = FMath::Clamp(
				TimeSinceStaminaDrain - FMath::Max(PrevElapsed, StaminaRegenDelay), 0.0f, DeltaSeconds);
			if (RegenSeconds > 0.0f && CurrentStamina < MaxStamina)
			{
				CurrentStamina = FMath::Min(MaxStamina, CurrentStamina + StaminaRegenPerSecond * RegenSeconds);
			}

			// 長時間放置しても値が際限なく増えないよう、遅延を超えた状態のまま上限を設ける。
			TimeSinceStaminaDrain = FMath::Min(TimeSinceStaminaDrain, StaminaRegenDelay + 1.0f);
		}
	}

	// 枯渇後の禁止はしきい値まで回復したら解除する。Dashの再開はStartDash（押し直し）だけが行う。
	if (bDashExhausted && CurrentStamina >= MaxStamina * DashRestartThresholdRatio)
	{
		bDashExhausted = false;
	}

	BroadcastStaminaChanged();
}

void ABoarPlayerCharacter::EndDashByExhaustion()
{
	bDashExhausted = true;

	if (!bIsDashing)
		return;
	// 急停止させず、Shiftを離したときと同じDash→Walkの速度遷移（CMCの既定の減速）に任せる。
	bIsDashing = false;
	ApplyGroundMoveSpeed();
	UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::BroadcastStaminaChanged(bool bForce)
{
	if (!bForce && CurrentStamina == LastBroadcastStamina && bDashExhausted == bLastBroadcastDashLocked)
		return;

	LastBroadcastStamina = CurrentStamina;
	bLastBroadcastDashLocked = bDashExhausted;
	OnStaminaChanged.Broadcast(CurrentStamina, MaxStamina, bDashExhausted);
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

	// 捕獲後も演出・檻収容のためActorは残るが、接触ダメージの対象からは即時除外する。
	const ABoarBase* Boar = Cast<ABoarBase>(OtherActor);
	if (!IsValid(Boar) || Boar->IsCaptured())
		return;
	// 攻撃（突進）中のイノシシに当たったときだけ被弾する。逃走・徘徊中などにぶつかっただけではスタンしない。
	if (!Boar->IsPlayerAttackHitActive())
		return;
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return;

	// 命中情報は被弾が成立する前の位置・移動状態から確定する（Gadget中断やノックバックで変わる前）。
	FBoarPlayerHitInfo HitInfo;
	HitInfo.Attacker = OtherActor;
	HitInfo.AttackerLocation = OtherActor->GetActorLocation();
	FVector Away = GetActorLocation() - HitInfo.AttackerLocation;
	Away.Z = 0.0;
	HitInfo.HitDirection = Away.IsNearlyZero() ? -GetActorForwardVector().GetSafeNormal2D() : Away.GetSafeNormal();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();

	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : BoarPlayer::FallbackCapsuleRadius;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : BoarPlayer::FallbackCapsuleHalfHeight;

	// 攻撃側を向いたCapsule表面の、胴（中心よりHalf Heightの15%上）の高さを命中位置とする。
	constexpr double TorsoHeightRatio = 0.15;
	HitInfo.HitLocation = GetActorLocation() - HitInfo.HitDirection * Radius + FVector(0.0, 0.0, HalfHeight * TorsoHeightRatio);
	HitInfo.bAirborne = GetCharacterMovement() && GetCharacterMovement()->IsFalling();

	// ダメージを受けてスタン状態にする
	HealthComponent->TakeDamage(ContactDamage);

	// HP0ではStunを開始せず、GameOver演出のダウンアニメがStun状態で上書きされないようにする。
	// カメラ・フラッシュ・SEはGameOver演出（AGameOverPresentationDirector）に任せ、
	// 最後の一撃が見えるよう衝撃波と頭上の星だけを被弾演出側で出す（bLethal）。
	if (HealthComponent->IsDead())
	{
		EnterDowned();
		HitInfo.bLethal = true;
		OnPlayerHitReceived.Broadcast(HitInfo);
		return;
	}

	// 多段ヒットを防ぐため、被弾後に短い無敵を付与する。Stun中は別途bIsStunnedで被弾を受け付けない。
	bIsInvincible = true;

	// 以前の無敵終了Timerが残っている場合は、新しい被弾時間を基準に再設定する。
	GetWorldTimerManager().ClearTimer(InvincibleTimerHandle);
	GetWorldTimerManager().SetTimer(InvincibleTimerHandle, this, &ABoarPlayerCharacter::EndInvincible,
		FMath::Max(InvincibleDuration, KINDA_SMALL_NUMBER), false);

	// Gameplayの結果（ダメージ成立・Stun開始）を確定してから演出へ通知する。
	// 演出側の失敗・欠落はこの後のStun進行に影響しない。
	BeginStun(HitInfo);
	OnPlayerHitReceived.Broadcast(HitInfo);
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
	FBoarPlayerHitInfo HitInfo;
	HitInfo.HitLocation = GetActorLocation();
	HitInfo.bAirborne = GetCharacterMovement() && GetCharacterMovement()->IsFalling();
	BeginStun(HitInfo);
}

void ABoarPlayerCharacter::BeginStun(const FBoarPlayerHitInfo& HitInfo)
{
	if (bIsDowned)
		return;

	bIsStunned = true;
	SetPlayerActionState(EPlayerActionState::Stun);

	// ガジェット使用中に被弾した場合はMontageや捕獲判定を中断扱いで終了する（空中アミの重力・AirControlもここで戻る）。
	EndGadgetUse(true);
	StopDash();

	// 押しっぱなしのJump入力がスタン解除後に勝手にジャンプとして残らないようにする。
	StopJumping();

	// このフレームまでに積まれた移動入力を捨て、Stun中の加速に使わせない。
	ConsumeMovementInputVector();
	bHadMoveInput = false;

	// ノックバックの速度方向へ振り向かないよう、Stun中は向きを固定する。
	RefreshOrientRotationToMovement();

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		const FVector Push = HitInfo.HitDirection.GetSafeNormal2D();
		if (bFaceAttackerOnStun && !Push.IsNearlyZero())
		{
			// 攻撃してきた方向を向き、後方へ倒れるリアクションで攻撃方向が伝わるようにする。
			SetActorRotation(FRotator(0.0, (-Push).Rotation().Yaw, 0.0));
		}

		if (Move->IsFalling())
		{
			// 空中では静止させず、落下を続けたまま攻撃方向へ流す（着地後にしりもちへ進む）。
			Move->Velocity.X = Push.X * StunAirborneKnockbackSpeed;
			Move->Velocity.Y = Push.Y * StunAirborneKnockbackSpeed;
		}
		else
		{
			// 地上では一度止めてから攻撃方向へ短く押し出す。入力はないため、通常の地上減速で自然に止まる。
			Move->StopMovementImmediately();

			if (Move->IsMovingOnGround())
				Move->Velocity = Push * StunKnockbackSpeed;
		}
	}

	// 連続してスタン処理が呼ばれた場合に備え、以前のTimerを破棄してから新しく設定する。
	GetWorldTimerManager().ClearTimer(StunTimerHandle);
	GetWorldTimerManager().ClearTimer(StunAirborneWaitTimerHandle);
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	bStunAwaitingLanding = Move && Move->IsFalling();

	// 開始通知は残り時間の計測（OnStunRecoveryStarted）より先に送る。
	OnStunStarted.Broadcast(HitInfo);

	if (!bIsStunned)
		return; // 通知先が同期的にダウン等で打ち切った場合

	if (bStunAwaitingLanding)
	{
		// 着地を待つ。落ち続けても上限でスタン時間の計測へ進み、操作不能が残り続けないようにする。
		if (StunAirborneMaxWait > 0.0f)
			GetWorldTimerManager().SetTimer(StunAirborneWaitTimerHandle, this, &ABoarPlayerCharacter::StartStunRecovery,
				StunAirborneMaxWait, false);
		else
			StartStunRecovery();
	}
	else
	{
		StartStunRecovery();
	}
}

void ABoarPlayerCharacter::StartStunRecovery()
{
	GetWorldTimerManager().ClearTimer(StunAirborneWaitTimerHandle);
	bStunAwaitingLanding = false;

	if (!bIsStunned || bIsDowned)
		return;

	GetWorldTimerManager().ClearTimer(StunTimerHandle);
	OnStunRecoveryStarted.Broadcast(StunDuration);

	if (!bIsStunned)
		return;
	// SetTimerは0秒で何もしないため、0秒設定は即時終了として扱う。
	if (StunDuration > 0.0f)
		GetWorldTimerManager().SetTimer(StunTimerHandle, this, &ABoarPlayerCharacter::EndStun, StunDuration, false);
	else
		EndStun();
}

void ABoarPlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// 着地音は1回の着地で通常／強着地のどちらか一方だけ。被弾の尻もち（HitReaction）と空中アミの叩き付けは各専用音に任せる。
	if (!bIsStunned && !bIsDowned && AirNetDropPhase == EAirNetDropPhase::None)
	{
		const float FallSpeed = -GetVelocity().Z;
		if (FallSpeed >= PlayerAudio.MinLandSpeed)
		{
			const bool bHard = FallSpeed >= PlayerAudio.HardLandSpeed && PlayerAudio.HardLand;
			PlayPlayerSound(bHard ? PlayerAudio.HardLand.Get() : PlayerAudio.Land.Get());
		}
	}

	if (!bIsStunned || !bStunAwaitingLanding)
		return;
	// 空中被弾の着地：横滑りしたまま座り込まないよう止めてから、しりもち→起き上がりの時間を数え始める。
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
		Move->StopMovementImmediately();

	StartStunRecovery();
}

void ABoarPlayerCharacter::PlayPlayerSound(USoundBase* Sound, float VolumeMultiplier) const
{
	if (!Sound || !GetGameInstance())
		return;

	if (auto* Audio = GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>())
		Audio->PlayWorldSoundEffect(const_cast<ABoarPlayerCharacter*>(this), Sound, GetActorLocation(), GetRootComponent(),
			PlayerAudio.Attenuation, VolumeMultiplier);
}

void ABoarPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Retry・Level遷移・破棄でStunが中断されても、演出側が頭上VFX等を確実に片付けられるよう終了を通知する。
	FinishStun(true);

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(InvincibleTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void ABoarPlayerCharacter::EnterDowned()
{
	if (bIsDowned)
		return;

	bIsDowned = true;

	// 既に開始していたStunのTimerで通常状態へ戻らないようにし、頭上VFX等の演出も中断扱いで終える。
	FinishStun(true);
	EndGadgetUse(true);
	StopDash();

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
		Move->StopMovementImmediately();

	SetPlayerActionState(EPlayerActionState::Down);
}

void ABoarPlayerCharacter::EndStun()
{
	FinishStun(false);
}

void ABoarPlayerCharacter::FinishStun(bool bInterrupted)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StunTimerHandle);
		World->GetTimerManager().ClearTimer(StunAirborneWaitTimerHandle);
	}

	bStunAwaitingLanding = false;
	const bool bWasStunned = bIsStunned;
	bIsStunned = false;

	if (!bWasStunned)
		return;

	// Stunが変更したのは向きの自動追従だけ。移動モード・IgnoreMoveInput等の他の所有者のロックには触れない。
	RefreshOrientRotationToMovement();

	OnStunEnded.Broadcast(bInterrupted);

	if (bInterrupted || bIsDowned || !GetWorld())
		return;
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return;

	// 起き上がった直後の再被弾を防ぐ。被弾から数えた無敵がまだ長く残っていれば、そちらを優先する。
	FTimerManager& Timers = GetWorldTimerManager();
	const float Remaining = Timers.IsTimerActive(InvincibleTimerHandle) ? Timers.GetTimerRemaining(InvincibleTimerHandle) : 0.0f;
	if (PostStunInvincibleDuration > Remaining)
	{
		bIsInvincible = true;
		Timers.SetTimer(InvincibleTimerHandle, this, &ABoarPlayerCharacter::EndInvincible, PostStunInvincibleDuration, false);
	}

	ApplyGroundMoveSpeed();

	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
		SetPlayerActionState(JumpCurrentCount >= 2 ? EPlayerActionState::DoubleJump : EPlayerActionState::Fall);
	else
		UpdateGroundActionState(bHadMoveInput);
}

void ABoarPlayerCharacter::RefreshOrientRotationToMovement()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move)
		return;

	// 書き手を一か所にし、状態フラグから毎回決め直す。保存・復元の順序に依存しないので、連続Stunや
	// ガジェット使用中の被弾（BeginStun内のEndGadgetUse）でも、固定中の値を「本来の値」として覚え込まない。
	const bool bGadgetLocksFacing = bIsGadgetInUse && !bGadgetUseAllowsMovement;
	Move->bOrientRotationToMovement = !bIsStunned && !bGadgetLocksFacing;
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
	bGadgetUseAllowsMovement = false;

	// 中断時は地上用Montageも止め、後続の捕獲Notifyを再生させない。
	// 使用中フラグを先に下ろしているため、停止によるInterrupted通知が再入しても終了処理は重複しない。
	// OneShotの通常終了（OnCompleted）では既に再生が終わっているので、記録だけを消す。
	// Holdの通常終了（ボタン解除・クイック切替）では構えMontageが自然終了しないため、今回の使用分だけを止める。
	if (bWasInterrupted || CurrentGadgetUseStyle == EGadgetUseStyle::Hold)
	{
		StopGadgetUseMontage();
	}
	else
	{
		GadgetUseMontage = nullptr;
		GadgetUseMontageInstanceID = INDEX_NONE;
	}

	RefreshOrientRotationToMovement();

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
	if (IsGameplayActionBlocked())
		return;

	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
		return;

	// 使用中の地上状態はUseGadgetを維持する。移動可能な使用でジャンプ後に着地した場合もここで戻す。
	if (bIsGadgetInUse)
	{
		SetPlayerActionState(EPlayerActionState::UseGadget);
		return;
	}

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

bool ABoarPlayerCharacter::IsGameplayActionBlocked() const
{
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return true;

	return bIsStunned || bIsDowned;
}

bool ABoarPlayerCharacter::CanUseMovementActions() const
{
	if (IsGameplayActionBlocked())
		return false;

	return !bIsGadgetInUse || bGadgetUseAllowsMovement;
}

bool ABoarPlayerCharacter::CanSwitchGadgetSlot() const
{
	if (IsGameplayActionBlocked())
		return false;
	if (!bIsGadgetInUse)
		return true;
	// OneShot（アミの振り・捕獲判定窓・空中アミ）は途中で切り替えず、完了または中断経路を待つ。
	return CurrentGadgetUseStyle == EGadgetUseStyle::Hold && AirNetDropPhase == EAirNetDropPhase::None;
}

bool ABoarPlayerCharacter::IsActionLocked() const
{
	return IsGameplayActionBlocked() || bIsGadgetInUse;
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
	return Cast<ANetGadget>(Gadget) && Move && Move->IsFalling();
}

bool ABoarPlayerCharacter::HasAirNetDropPresentation() const
{
	return AirNetDropAnticipationAnimation &&
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
	if (Move && Move->IsFalling() &&
		(AirNetDropPhase == EAirNetDropPhase::Swing || AirNetDropPhase == EAirNetDropPhase::Drop))
	{
		// Swingも急降下中として数える。見た目の再生速度・長さにかかわらず、
		// Gameplayの上限に達したフレームでは追加加速・掃引より先に終了する。
		AirNetDropDiveElapsed += FMath::Max(DeltaSeconds, 0.0f);

		if (AirNetDropDiveElapsed >= FMath::Max(0.0f, AirNetDropMaxDiveDuration))
		{
			BeginAirNetDropRecovery();
			return;
		}
	}

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

			if (!EnterAirNetDropPhase(EAirNetDropPhase::Swing))
				EndGadgetUse(true);
		}
		break;
	}
	case EAirNetDropPhase::Swing:
	case EAirNetDropPhase::Drop:
	{
		// 急降下：通常重力の代わりに設定加速度で加速し、最大落下速度で止める。
		Move->Velocity.Z = FMath::Max(Move->Velocity.Z - AirNetDropDownAcceleration * DeltaSeconds, -AirNetDropMaxFallSpeed);

		if (AirNetDropPhase == EAirNetDropPhase::Swing)
		{
			const float SwingDuration = AirNetDropSwingAnimation->GetPlayLength() / FMath::Max(0.1f, AirNetDropSwingPlayRate);
			if (AirNetDropPhaseElapsed >= FMath::Max(0.0f, SwingDuration - AirNetDropBlendTime))
			{
				if (!EnterAirNetDropPhase(EAirNetDropPhase::Drop))
					EndGadgetUse(true);
			}
		}
		break;
	}
	default:
		break;
	}
}

void ABoarPlayerCharacter::BeginAirNetDropRecovery()
{
	// Capture OFF Notifyが未到達でも、時間上限を過ぎて捕獲性能を延長しない。
	if (ANetGadget* NetGadget = GetEquippedNetGadget())
		NetGadget->EndCaptureWindow();

	RestoreAirNetDropMovement();

	if (!EnterAirNetDropPhase(EAirNetDropPhase::Recovery))
		EndGadgetUse(true);
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
		// 着地か最大急降下時間まで構えを保持する。
		return PlayAirNetDropAnimation(AirNetDropLoopAnimation, 1.0f, BoarPlayer::AirNetDropLoopCount, 0.0f);
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

		if (!EnterAirNetDropPhase(EAirNetDropPhase::Swing))
			EndGadgetUse(true);

		break;
	case EAirNetDropPhase::Swing:
		if (!EnterAirNetDropPhase(EAirNetDropPhase::Drop))
			EndGadgetUse(true);

		break;
	case EAirNetDropPhase::Drop:
		BeginAirNetDropRecovery();
		break;
	default:
		// 引き戻し・着地の完了で空中アミ捕獲を終える。
		FinishGadgetUseAnimation(false);
		break;
	}
}
