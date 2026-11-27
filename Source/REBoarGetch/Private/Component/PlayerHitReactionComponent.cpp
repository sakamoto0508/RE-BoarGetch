#include "Component/PlayerHitReactionComponent.h"

#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraSubsystem.h"
#include "Effects/ACFCameraEffect.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Presentation/BoarImpactShockwave.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBoarHitReaction, Log, All);

namespace BoarHitReaction
{
	/** 攻撃者が無い被弾で、攻撃の発生源とみなす距離（cm、命中位置から被弾方向の逆へ）。 */
	constexpr float FallbackSourceDistance = 100.0f;

	/** 頭のSocketが無いとき、Stun VFXを置くActor原点からの高さ（cm）。 */
	constexpr float StunVFXFallbackHeight = 80.0f;

	/** Niagaraの色はUser Parameter「Color」で受けます（捕獲・Stage Clearの既存Systemと同じ規約）。 */
	const FName ColorParameter(TEXT("Color"));

	/** PIEで新旧の衝撃波を見比べるためのConsole Variableです。 */
	TAutoConsoleVariable<int32> CVarUseLegacyImpactVFX(
		TEXT("boar.HitVFX.UseLegacy"), -1,
		TEXT("赤いBoarに当たったときの衝撃波: -1 = HitReactionComponentの設定（既定）、0 = 新しいインパクト、1 = 従来のリング。"),
		ECVF_Default);
}

UPlayerHitReactionComponent::UPlayerHitReactionComponent()
{
	// Hit Flashのフェードと頭上VFXの追従のときだけTickする。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 既定のアセット。BP_PlayerCharacterのHitReactionComponentで差し替え・解除できる（AirNetDropと同じ方式）。
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ImpactFinder(
		TEXT("/Game/REBoarGetch/Art/VFX/Player/NS_PlayerHitImpact"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> LegacyImpactFinder(
		TEXT("/Game/REBoarGetch/Art/VFX/Player/NS_PlayerHitShockwave"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> StunFinder(
		TEXT("/Game/REBoarGetch/Art/VFX/Player/NS_PlayerStunOrbit"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ImpactSoundFinder(
		TEXT("/Game/REBoarGetch/Art/Audio/Player/SFX_PlayerHit_Impact"));
	static ConstructorHelpers::FObjectFinder<USoundBase> StunSoundFinder(
		TEXT("/Game/REBoarGetch/Art/Audio/Player/SFX_PlayerStun_Dizzy"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SitSoundFinder(
		TEXT("/Game/REBoarGetch/Art/Audio/GameOver/SFX_GameOver_BodyFall"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlashFinder(
		TEXT("/Game/REBoarGetch/Art/VFX/Capture/M_CaptureGlow"));
	static ConstructorHelpers::FObjectFinder<UACFCameraEffectPreset> CameraFinder(
		TEXT("/Game/DataAssets/Camera/DA_PlayerHitCameraEffect"));
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> KnockdownFinder(
		TEXT("/Game/Animation/Player/HitReaction/A_Player_HitKnockdown"));
	ImpactVFX = ImpactFinder.Object;
	LegacyImpactVFX = LegacyImpactFinder.Object;
	StunVFX = StunFinder.Object;
	ImpactSound = ImpactSoundFinder.Object;
	StunSound = StunSoundFinder.Object;
	SitImpactSound = SitSoundFinder.Object;
	HitFlashMaterial = FlashFinder.Object;
	HitCameraEffect = CameraFinder.Object;
	KnockdownAnimation = KnockdownFinder.Object;
}

void UPlayerHitReactionComponent::BeginPlay()
{
	Super::BeginPlay();
	ABoarPlayerCharacter* Player = GetPlayer();
	if (!Player)
	{
		UE_LOG(LogBoarHitReaction, Warning, TEXT("%s がABoarPlayerCharacterに付いていないため、被弾演出を無効にします。"), *GetPathName());
		return;
	}

	Player->OnPlayerHitReceived.AddUniqueDynamic(this, &UPlayerHitReactionComponent::HandleHitReceived);
	Player->OnStunStarted.AddUniqueDynamic(this, &UPlayerHitReactionComponent::HandleStunStarted);
	Player->OnStunRecoveryStarted.AddUniqueDynamic(this, &UPlayerHitReactionComponent::HandleStunRecoveryStarted);
	Player->OnStunEnded.AddUniqueDynamic(this, &UPlayerHitReactionComponent::HandleStunEnded);
	bBound = true;
}

void UPlayerHitReactionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Retry・Level遷移・破棄で、Overlay Material・頭上VFX・Camera Effect・Timerを残さない。
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(SitImpactTimerHandle);

	StopHitFlash();
	StopStunVFX(true);
	bStunPresentationActive = false;
	ReleaseCameraEffect(true);

	if (ABoarPlayerCharacter* Player = GetPlayer(); Player && bBound)
	{
		Player->OnPlayerHitReceived.RemoveDynamic(this, &UPlayerHitReactionComponent::HandleHitReceived);
		Player->OnStunStarted.RemoveDynamic(this, &UPlayerHitReactionComponent::HandleStunStarted);
		Player->OnStunRecoveryStarted.RemoveDynamic(this, &UPlayerHitReactionComponent::HandleStunRecoveryStarted);
		Player->OnStunEnded.RemoveDynamic(this, &UPlayerHitReactionComponent::HandleStunEnded);
	}

	bBound = false;
	Super::EndPlay(EndPlayReason);
}

void UPlayerHitReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HitFlashMID)
	{
		HitFlashElapsed += FMath::Max(DeltaTime, 0.0f);
		const float FadeT = HitFlashFadeDuration > 0.0f
			? FMath::Clamp((HitFlashElapsed - HitFlashHoldDuration) / HitFlashFadeDuration, 0.0f, 1.0f)
			: (HitFlashElapsed >= HitFlashHoldDuration ? 1.0f : 0.0f);
		// 立ち上がりは瞬時、消えるときは二乗で素早く落とす（一瞬で認識でき、残光は短く）。
		const float Amount = HitFlashIntensity * FMath::Square(1.0f - FadeT);
		HitFlashMID->SetScalarParameterValue(HitFlashAmountParameter, Amount);

		if (FadeT >= 1.0f)
			StopHitFlash();
	}

	if (IsStunVFXActive())
		UpdateStunVFXLocation();

	RefreshTickEnabled();
}

//-------------------------------------------------
// ---- Gameplayからの通知 ----
//-------------------------------------------------

void UPlayerHitReactionComponent::HandleHitReceived(const FBoarPlayerHitInfo& HitInfo)
{
	++ImpactPresentationCount;
	PlayImpactVFX(HitInfo);

	if (HitInfo.bLethal)
	{
		// GameOverの最後の一撃：衝撃波に加えて頭上の星を出し、倒れたまま残す（Retry・Level遷移のEndPlayで消える）。
		// カメラ・赤フラッシュ・ヒットストップ・SEはGameOver演出が担当するので、ここでは重ねない。
		StartStunVFX();
		return;
	}

	PlaySound2D(ImpactSound, ImpactSoundVolume);
	StartHitFlash();
	PlayCameraReaction(HitInfo);
}

void UPlayerHitReactionComponent::HandleStunStarted(const FBoarPlayerHitInfo& HitInfo)
{
	(void)HitInfo;
	bStunPresentationActive = true;
	StartStunVFX();
	PlaySound2D(StunSound, StunSoundVolume);
}

void UPlayerHitReactionComponent::HandleStunRecoveryStarted(float RecoveryDuration)
{
	PlayKnockdown(RecoveryDuration);
}

void UPlayerHitReactionComponent::HandleStunEnded(bool bInterrupted)
{
	bStunPresentationActive = false;

	if (!bInterrupted)
		PlaySound2D(StunRecoveredSound, StunRecoveredSoundVolume);

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(SitImpactTimerHandle);

	StopStunVFX(bInterrupted);

	// 正常終了ではAnimationが既に起き上がり・BlendOut中。再生速度の上下限で尺が余った場合だけ止める。
	// 中断（ダウン等）では後続のGameOver演出のMontageへ素早く譲る。自分のMontageインスタンスだけを止める。
	// 中断時は素早く（0.1秒で）抜け、通常終了時は設定どおりのBlend Outで戻す。
	constexpr float InterruptedBlendOut = 0.1f;
	StopKnockdown(bInterrupted ? InterruptedBlendOut : KnockdownBlendOut);
}

//-------------------------------------------------
// 命中インパクト
//-------------------------------------------------

void UPlayerHitReactionComponent::PlayImpactVFX(const FBoarPlayerHitInfo& HitInfo)
{
	UWorld* World = GetWorld();
	const int32 LegacyOverride = BoarHitReaction::CVarUseLegacyImpactVFX.GetValueOnGameThread();
	const bool bLegacy = LegacyOverride >= 0 ? LegacyOverride != 0 : bUseLegacyImpactVFX;
	UNiagaraSystem* System = bLegacy ? LegacyImpactVFX.Get() : ImpactVFX.Get();
	if (!System || !World)
		return;

	// 有効Hitは無敵・Stunで1回に限られるが、同じ瞬間の重複通知があっても衝撃波は1つだけにする。
	const double Now = World->GetTimeSeconds();
	if (LastImpactVFXTime >= 0.0 && Now - LastImpactVFXTime < MinImpactVFXInterval)
		return;

	LastImpactVFXTime = Now;

	// 突進方向（イノシシ→プレイヤーへ押し込む向き、水平）。命中情報が無ければ位置関係、最後はプレイヤーの後ろ向き。
	FVector Direction = HitInfo.HitDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero() && HitInfo.Attacker)
		Direction = (GetOwner()->GetActorLocation() - HitInfo.AttackerLocation).GetSafeNormal2D();

	if (Direction.IsNearlyZero())
		Direction = -GetOwner()->GetActorForwardVector().GetSafeNormal2D();

	if (Direction.IsNearlyZero())
		Direction = FVector::ForwardVector;

	const FRotator Rotation = FRotationMatrix::MakeFromX(Direction).Rotator();

	if (bLegacy)
	{
		if (UNiagaraComponent* VFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, System, HitInfo.HitLocation, Rotation, FVector(ImpactVFXScale), true, true, ENCPoolMethod::AutoRelease))
		{
			VFX->SetVariableLinearColor(BoarHitReaction::ColorParameter, LegacyImpactVFXColor);
			++ImpactVFXSpawnCount;
		}

		return;
	}

	// 衝撃波の向き・User Parameterの設定は檻への攻撃命中（UCageVisualComponent）と共通（BoarImpactShockwave）。
	FBoarImpactShockwaveParams Params;
	Params.Scale = ImpactVFXScale;
	Params.Color = ImpactVFXColor;
	Params.RingCameraBias = ImpactRingCameraBias;
	const APlayerController* PC = GetLocalController();
	if (BoarImpactShockwave::Spawn(World, System, HitInfo.HitLocation, Direction, PC ? PC->PlayerCameraManager.Get() : nullptr, Params))
		++ImpactVFXSpawnCount;
}

void UPlayerHitReactionComponent::PlayCameraReaction(const FBoarPlayerHitInfo& HitInfo)
{
	APlayerController* PC = GetLocalController();
	if (!PC)
		return;

	const FVector Source = HitInfo.Attacker
		? HitInfo.AttackerLocation
		: HitInfo.HitLocation - HitInfo.HitDirection * BoarHitReaction::FallbackSourceDistance;

	// 連続被弾でも揺れを積み重ねず、前の被弾の揺れは即座に置き換える。
	ReleaseCameraEffect(true);
	UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(PC);
	if (HitCameraEffect && Cameras && Cameras->CanPlayCameraEffects())
	{
		FACFCameraEffectParams Params;
		Params.Scale = HitCameraEffectScale;
		Params.bUseSourceLocation = true;
		Params.SourceLocation = Source;
		Params.Direction = HitInfo.HitDirection;

		// このComponentが破棄されたらACF側でも止まる。
		Params.Instigator = this;
		ActiveCameraEffect = Cameras->PlayCameraEffect(HitCameraEffect, Params);
		if (ActiveCameraEffect.IsValid())
			return;
	}

	// ACFカメラが無い（Console Variableで無効化等）ときだけ、SpringArmカメラへUE Camera Shakeを使う。
	if (FallbackCameraShake && !FallbackCameraShake->HasAnyClassFlags(CLASS_Abstract) && PC->PlayerCameraManager)
		PC->PlayerCameraManager->StartCameraShake(FallbackCameraShake, FallbackCameraShakeScale);
}

void UPlayerHitReactionComponent::ReleaseCameraEffect(bool bImmediate)
{
	if (!ActiveCameraEffect.IsValid())
		return;
	if (APlayerController* PC = GetLocalController())
		if (UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(PC))
			Cameras->StopCameraEffect(ActiveCameraEffect, bImmediate);
	// 自分が再生したEffectだけを止める（Stop Allは使わない）。
	ActiveCameraEffect.Invalidate();
}

void UPlayerHitReactionComponent::PlaySound2D(USoundBase* Sound, float Volume) const
{
	if (!Sound || Volume <= 0.0f)
		return;

	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (UBoarAudioManagerSubsystem* Audio = GameInstance ? GameInstance->GetSubsystem<UBoarAudioManagerSubsystem>() : nullptr)
		Audio->PlaySoundEffect2D(const_cast<UPlayerHitReactionComponent*>(this), Sound, Volume, 1.0f);
}

//-------------------------------------------------
// ---- 被弾フラッシュ ----
//-------------------------------------------------

void UPlayerHitReactionComponent::StartHitFlash()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || !HitFlashMaterial || HitFlashIntensity <= 0.0f)
		return;
	if (!HitFlashMID)
	{
		HitFlashMID = UMaterialInstanceDynamic::Create(HitFlashMaterial, this);
		if (!HitFlashMID)
			return;

		HitFlashOriginalOverlay = Mesh->GetOverlayMaterial();
		Mesh->SetOverlayMaterial(HitFlashMID);
	}

	HitFlashElapsed = 0.0f;
	HitFlashMID->SetVectorParameterValue(HitFlashColorParameter, HitFlashColor);
	HitFlashMID->SetScalarParameterValue(HitFlashAmountParameter, HitFlashIntensity);
	RefreshTickEnabled();
}

void UPlayerHitReactionComponent::StopHitFlash()
{
	if (!HitFlashMID)
		return;
	// 自分が設定したOverlayのときだけ戻す。後から別の演出が設定したOverlayは上書きしない。
	if (USkeletalMeshComponent* Mesh = GetMesh(); Mesh && Mesh->GetOverlayMaterial() == HitFlashMID)
		Mesh->SetOverlayMaterial(HitFlashOriginalOverlay);

	HitFlashMID = nullptr;
	HitFlashOriginalOverlay = nullptr;
	HitFlashElapsed = 0.0f;
}

//-------------------------------------------------
// 頭上のStun表示
//-------------------------------------------------

bool UPlayerHitReactionComponent::IsStunVFXActive() const
{
	return IsValid(StunVFXComponent) && StunVFXComponent->IsActive();
}

void UPlayerHitReactionComponent::StartStunVFX()
{
	if (!StunVFX || !GetWorld())
		return;
	// 連続Stunでも1つだけ表示する（既に出ていれば継続）。
	if (IsStunVFXActive())
		return;

	StopStunVFX(true);
	StunVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(), StunVFX, GetOwner()->GetActorLocation(), FRotator::ZeroRotator, FVector(StunVFXScale), false, false,
		ENCPoolMethod::None);
	if (!StunVFXComponent)
		return;

	StunVFXComponent->SetVariableLinearColor(BoarHitReaction::ColorParameter, StunVFXColor);
	UpdateStunVFXLocation();
	StunVFXComponent->Activate(true);
	RefreshTickEnabled();
}

void UPlayerHitReactionComponent::StopStunVFX(bool bImmediate)
{
	if (!StunVFXComponent)
		return;

	UNiagaraComponent* VFX = StunVFXComponent;
	StunVFXComponent = nullptr;

	if (!IsValid(VFX))
		return;
	// 「操作不能」の表示は操作が戻った瞬間に消す（粒子の余韻を残すと、まだ動けないように見えるため）。
	(void)bImmediate;
	VFX->DeactivateImmediate();
	VFX->DestroyComponent();
}

void UPlayerHitReactionComponent::UpdateStunVFXLocation() const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (!StunVFXComponent || !Mesh)
		return;
	// 頭のBoneの真上（World上方向）へ置く。しりもちで頭が傾いても周回面は水平のまま。
	const FVector Head = Mesh->DoesSocketExist(StunVFXSocket)
		? Mesh->GetSocketLocation(StunVFXSocket)
		: GetOwner()->GetActorLocation() + FVector(0.0, 0.0, BoarHitReaction::StunVFXFallbackHeight);

	StunVFXComponent->SetWorldLocation(Head + FVector(0.0, 0.0, StunVFXHeight));
}

//-------------------------------------------------
// しりもち・起き上がり
//-------------------------------------------------

bool UPlayerHitReactionComponent::IsKnockdownAnimationPlaying() const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || !KnockdownMontage || KnockdownMontageInstanceID == INDEX_NONE)
		return false;

	const FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(KnockdownMontageInstanceID);
	return Instance && Instance->IsActive() && Instance->Montage == KnockdownMontage;
}

void UPlayerHitReactionComponent::PlayKnockdown(float RecoveryDuration)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || !KnockdownAnimation)
		return;

	StopKnockdown(KnockdownBlendIn);

	const float Length = KnockdownAnimation->GetPlayLength();
	if (Length <= KINDA_SMALL_NUMBER)
		return;
	// 起き上がり完了（BlendOut完了）をStun解除に合わせる。尺が極端な場合は自然に見える範囲で止める。
	const float PlayRate = RecoveryDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp(Length / RecoveryDuration, KnockdownMinPlayRate, FMath::Max(KnockdownMinPlayRate, KnockdownMaxPlayRate))
		: KnockdownMaxPlayRate;

	KnockdownMontage = Anim->PlaySlotAnimationAsDynamicMontage(KnockdownAnimation, KnockdownSlotName,
		KnockdownBlendIn, KnockdownBlendOut, PlayRate, 1, -1.0f, 0.0f);
	if (!KnockdownMontage)
	{
		UE_LOG(LogBoarHitReaction, Warning, TEXT("Slot %s でダウンアニメーションを再生できませんでした。"), *KnockdownSlotName.ToString());
		return;
	}

	const FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(KnockdownMontage);
	KnockdownMontageInstanceID = Instance ? Instance->GetInstanceID() : INDEX_NONE;

	if (SitImpactSound && GetWorld())
	{
		const float Delay = SitImpactNormalizedTime * Length / PlayRate;
		GetWorld()->GetTimerManager().SetTimer(SitImpactTimerHandle, this, &UPlayerHitReactionComponent::HandleSitImpact,
			FMath::Max(Delay, KINDA_SMALL_NUMBER), false);
	}
}

void UPlayerHitReactionComponent::HandleSitImpact()
{
	// しりもちのMontageが別の演出に割り込まれていたら鳴らさない。
	if (IsKnockdownAnimationPlaying())
		PlaySound2D(SitImpactSound, SitImpactSoundVolume);
}

void UPlayerHitReactionComponent::StopKnockdown(float BlendOutTime)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = KnockdownMontage;
	const int32 InstanceID = KnockdownMontageInstanceID;
	KnockdownMontage = nullptr;
	KnockdownMontageInstanceID = INDEX_NONE;

	if (!Anim || !Montage || InstanceID == INDEX_NONE)
		return;

	FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(InstanceID);

	// 既にBlendOut中・終了済み、別Montageに置き換わっている場合は触らない（GameOver等の後続Montageを止めない）。
	if (!Instance || !Instance->IsActive() || Instance->Montage != Montage)
		return;

	Instance->Stop(FAlphaBlend(FMath::Max(BlendOutTime, 0.0f)));
}

//-------------------------------------------------
// Helpers
//-------------------------------------------------

ABoarPlayerCharacter* UPlayerHitReactionComponent::GetPlayer() const
{
	return Cast<ABoarPlayerCharacter>(GetOwner());
}

USkeletalMeshComponent* UPlayerHitReactionComponent::GetMesh() const
{
	const ABoarPlayerCharacter* Player = GetPlayer();
	return Player ? Player->GetMesh() : nullptr;
}

APlayerController* UPlayerHitReactionComponent::GetLocalController() const
{
	const ABoarPlayerCharacter* Player = GetPlayer();
	APlayerController* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	return PC && PC->IsLocalController() ? PC : nullptr;
}

void UPlayerHitReactionComponent::RefreshTickEnabled()
{
	SetComponentTickEnabled(HitFlashMID != nullptr || IsStunVFXActive());
}
