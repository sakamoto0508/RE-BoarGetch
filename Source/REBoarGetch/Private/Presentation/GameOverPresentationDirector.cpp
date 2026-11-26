#include "Presentation/GameOverPresentationDirector.h"
#include "REBoarGetch.h"

#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Boar/BoarBase.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/BoarPlayerCharacter.h"
#include "Player/BoarPlayerController.h"
#include "UI/BoarGameOverEffectWidget.h"

namespace GameOverPresentation
{
	/** 画面効果WidgetのZOrder（HUDより前、GameOver UIより後ろ）。 */
	constexpr int32 EffectWidgetZOrder = 50;

	/** 0除算・極端に短い演出を避けるための各時間の下限（秒）。 */
	constexpr float MinFlashDuration = .02f;
	constexpr float MinKnockDuration = .05f;
	constexpr float MinGlowDecay = .05f;
	constexpr float MinFadeDuration = .01f;
	constexpr float MinNoiseDuration = .02f;
	constexpr float MinCameraDuration = .1f;
	constexpr float MinCameraRiseDuration = .05f;

	/** Hit Stop中の時間倍率の下限と、CustomTimeDilationの計算で0除算を避ける値。 */
	constexpr float MinHitStopTimeScale = .01f;
	constexpr float MinTimeDilation = .0001f;

	/** ダウンアニメーションの再生速度の下限。 */
	constexpr float MinDownPlayRate = .1f;

	/** ダウン前に再生中のMontageを止めるBlend時間と、演出終了時にダウンMontageを止めるBlend時間（秒）。 */
	constexpr float StopOtherMontagesBlend = .08f;
	constexpr float StopDownMontageBlend = .1f;

	/** ダウンMontageの終端からこの秒数手前で一時停止し、倒れた姿勢を保つ（終端まで行くとAnimBPへ戻るため）。 */
	constexpr float DownHoldBeforeEnd = .04f;

	/** Montageが見た目に反映されているかを確認するまで待つフレーム数（Slotの重みは1フレーム遅れて入る）。 */
	constexpr int32 MontageCheckFrame = 2;

	/** CapsuleのHalf Heightが取れないときの既定値と、接地判定でCapsuleの下端からさらに調べる距離（cm）。 */
	constexpr float DefaultCapsuleHalfHeight = 90.f;
	constexpr float GroundProbeExtra = 12.f;

	/** 空中でGameOverになったとき、着地を待つ最長時間（秒）。これを過ぎたら空中のまま止める。 */
	constexpr float LandingWaitTimeout = 3.f;

	/** カメラ側にいるとみなすBoarの向きの内積と、横へ逃がすときにカメラから離す割合。 */
	constexpr float CameraSideDot = .3f;
	constexpr float CameraSideAwayRatio = .3f;

	/** カメラの注視点を寄せるときのSweep球の半径（cm）と、当たった位置から手前に戻す割合。 */
	constexpr float FocusProbeRadius = 12.f;
	constexpr float FocusProbeBackOff = .05f;

	/** カメラの持ち上げとBoarの跳ね返りのEase Outの強さ。 */
	constexpr float CameraRiseEaseExponent = 2.f;
	constexpr float KnockbackEaseExponent = 2.f;

	// ---- ガジェットの電源が落ちる発光 ----

	/** 発光が最大になるまでの時間（秒）と、減衰カーブの指数。 */
	constexpr float GlowRiseTime = .07f;
	constexpr float GlowDecayExponent = 1.6f;

	/** 減衰の後半（この割合以降）で明滅させる。Sinの角速度とその閾値、明滅中の明るさの倍率。 */
	constexpr float GlowFlickerStartRatio = .35f;
	constexpr float GlowFlickerSpeed = 47.f;
	constexpr float GlowFlickerThreshold = -.25f;
	constexpr float GlowFlickerDim = .3f;
}

UBoarGameOverEffectWidget* AGameOverPresentationDirector::EnsureEffectWidget()
{
	if (EffectWidget || bEffectWidgetUnavailable)
		return EffectWidget;

	ABoarPlayerController* PC = GetPresentationController();
	if (!PC)
		return nullptr;

	// 画面効果Widget（HUDより前、GameOver UIより後ろ）。代替Widgetは使わず、使えない場合は画面効果だけを省く。
	// GameOverの進行・カメラ・Result表示はWidgetの有無に関係なく続行する。
	auto Skip = [this](const FString& Reason)
	{
		UE_LOG(LogREBoarGetch, Error, TEXT("[GameOver] %s: %s。画面効果を省きます。"), *GetName(), *Reason);
		bEffectWidgetUnavailable = true;
		return nullptr;
	};

	if (!EffectWidgetClass || EffectWidgetClass->HasAnyClassFlags(CLASS_Native))
		return Skip(TEXT("EffectWidgetClass（WBP_GameOverEffect）が未設定です"));

	EffectWidget = CreateWidget<UBoarGameOverEffectWidget>(PC, EffectWidgetClass);
	if (!EffectWidget)
		return Skip(FString::Printf(TEXT("%s を生成できません"), *EffectWidgetClass->GetName()));

	const TArray<FString> Missing = EffectWidget->GetMissingLayerNames();
	if (Missing.Num() == UBoarGameOverEffectWidget::LayerCount)
	{
		EffectWidget = nullptr;
		return Skip(FString::Printf(TEXT("%s にLayerのBindingが1つもありません"), *EffectWidgetClass->GetName()));
	}

	if (!Missing.IsEmpty())
	{
		UE_LOG(LogREBoarGetch, Error, TEXT("[GameOver] %s: %s にLayerのBindingがありません（%s）。該当するLayerを省きます。"),
			*GetName(), *EffectWidgetClass->GetName(), *FString::Join(Missing, TEXT(", ")));
	}

	EffectWidget->AddToViewport(GameOverPresentation::EffectWidgetZOrder);
	return EffectWidget;
}

void AGameOverPresentationDirector::BeginRedFlash(float PeakAlpha, float Duration)
{
	FlashPeak = PeakAlpha;
	FlashDuration = FMath::Max(GameOverPresentation::MinFlashDuration, Duration);
	FlashAge = 0.f;

	if (UBoarGameOverEffectWidget* W = EnsureEffectWidget())
	{
		W->SetFlashAlpha(FlashPeak);
	}
}

void AGameOverPresentationDirector::BeginHitStop(float Duration, float TimeScale)
{
	using namespace GameOverPresentation;

	if (Duration <= 0.f || bHitStopActive)
		return;

	// 排他の前提：GameOver確定時（ResolveStageEnd）に捕獲演出は中断・復元済みで、以降はCanAdvanceStage()により
	// 新しい捕獲演出も始まらない。そのためこの間のGlobal Time Dilationの所有者はこの演出だけになる。
	OriginalTimeDilation = UGameplayStatics::GetGlobalTimeDilation(this);
	const float Scale = FMath::Clamp(TimeScale, MinHitStopTimeScale, 1.f);
	UGameplayStatics::SetGlobalTimeDilation(this, OriginalTimeDilation * Scale);

	// 同じフレーム内の後続Delayも実時間で進むよう、即座に打ち消します。
	CustomTimeDilation = 1.f / FMath::Max(OriginalTimeDilation * Scale, MinTimeDilation);

	HitStopRemaining = Duration;
	bHitStopActive = true;
}

void AGameOverPresentationDirector::EndHitStop()
{
	if (!bHitStopActive)
		return;

	bHitStopActive = false;

	if (GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(this, OriginalTimeDilation);
	}
}

void AGameOverPresentationDirector::SettlePlayerOnGround()
{
	using namespace GameOverPresentation;

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	UCharacterMovementComponent* Move = Player ? Player->GetCharacterMovement() : nullptr;
	if (!Move)
		return;

	const float Half = Player->GetCapsuleComponent()
		? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: DefaultCapsuleHalfHeight;

	// 足元に地面が無ければ落下させ、着地を待ってから止める（TickPresentation）。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GameOverGround), false, Player);
	const FVector From = Player->GetActorLocation();
	if (!GetWorld()->LineTraceTestByChannel(From, From - FVector(0, 0, Half + GroundProbeExtra), ECC_Visibility, Params))
	{
		Move->SetMovementMode(MOVE_Falling);
		Move->Velocity = FVector(0, 0, FMath::Min(0.f, Move->Velocity.Z));
		bWaitingForLanding = true;
	}
}

void AGameOverPresentationDirector::KnockBackNearbyBoars(float Radius, float Distance, float HopHeight, float Duration)
{
	using namespace GameOverPresentation;

	Knockbacks.Reset();
	KnockAge = 0.f;
	KnockDuration = FMath::Max(MinKnockDuration, Duration);
	KnockHop = HopHeight;

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	const APlayerController* PC = GetPresentationController();

	if (!Player || Radius <= 0.f || Distance <= 0.f)
		return;

	const FVector P = Player->GetActorLocation();
	const FVector ToCamera = (PC && PC->PlayerCameraManager)
		? (PC->PlayerCameraManager->GetCameraLocation() - P).GetSafeNormal2D()
		: FVector::ZeroVector;

	for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
	{
		if (It->IsCaptured() || FVector::Dist2D(It->GetActorLocation(), P) > Radius)
			continue;

		FVector Dir = (It->GetActorLocation() - P).GetSafeNormal2D();
		if (Dir.IsNearlyZero())
		{
			Dir = -Player->GetActorForwardVector().GetSafeNormal2D();
		}

		// カメラとプレイヤーの間にいるBoarは横へ逃がし、ダウンの見え方を遮らないようにします。
		if (!ToCamera.IsNearlyZero() && FVector::DotProduct(Dir, ToCamera) > CameraSideDot)
		{
			const FVector Side = FVector::CrossProduct(FVector::UpVector, ToCamera);
			const float SideSign = FVector::DotProduct(Dir, Side) >= 0.f ? 1.f : -1.f;
			Dir = (Side * SideSign - ToCamera * CameraSideAwayRatio).GetSafeNormal2D();
		}

		Knockbacks.Add({*It, It->GetActorLocation(), Dir * Distance});
	}
}

float AGameOverPresentationDirector::PlayDownAnimation()
{
	using namespace GameOverPresentation;

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (bDownStarted || !Player)
		return 0.f;

	bDownStarted = true;
	USkeletalMeshComponent* Mesh = Player->GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const float Rate = FMath::Max(MinDownPlayRate, DownPlayRate);

	// 被弾時の再生中Montage（ガジェット等）を止めてから、FullBody Slotでダウンを再生します。
	if (Anim)
	{
		Anim->Montage_Stop(StopOtherMontagesBlend);
	}

	if (Anim && DownMontage && Anim->Montage_Play(DownMontage, Rate) > 0.f)
		return DownMontage->GetPlayLength() / Rate;

	if (Mesh && DownAnimation && bAllowSingleNodeFallback)
	{
		Mesh->PlayAnimation(DownAnimation, false);
		Mesh->SetPlayRate(Rate);
		bSingleNode = bDownHeld = true;
		return DownAnimation->GetPlayLength() / Rate;
	}

	UE_LOG(LogREBoarGetch, Warning, TEXT("[GameOver] ダウンアニメーションが未設定のため、カメラとUIだけで続行します。"));
	return 0.f;
}

void AGameOverPresentationDirector::UpdateDown()
{
	using namespace GameOverPresentation;

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player || !DownMontage || bSingleNode)
		return;

	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	if (!Anim)
		return;

	if (bAllowSingleNodeFallback && ++MontageCheckFrames == MontageCheckFrame)
	{
		// Slotが現在のAnimGraph経路に無いとMontageは見た目に反映されません。その場合は同じアニメをSingle Nodeで再生します。
		const FName Slot = DownMontage->SlotAnimTracks.IsEmpty() ? NAME_None : DownMontage->SlotAnimTracks[0].SlotName;

		UAnimSequenceBase* Sequence = DownAnimation.Get();
		if (!Sequence && !DownMontage->SlotAnimTracks.IsEmpty()
			&& !DownMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
		{
			Sequence = DownMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
		}

		if (Sequence && Anim->GetSlotNodeGlobalWeight(Slot) <= KINDA_SMALL_NUMBER)
		{
			const float Position = Anim->Montage_GetPosition(DownMontage);
			Anim->Montage_Stop(0.f, DownMontage);
			Player->GetMesh()->PlayAnimation(Sequence, false);
			Player->GetMesh()->SetPosition(Position, false);
			Player->GetMesh()->SetPlayRate(FMath::Max(MinDownPlayRate, DownPlayRate));
			bSingleNode = bDownHeld = true;

			UE_LOG(LogREBoarGetch, Log,
				TEXT("[GameOver] Slot '%s' がAnimGraphで有効ではないため、%s を単体ノードとして再生します。"),
				*Slot.ToString(), *GetNameSafe(Sequence));
			return;
		}
	}

	if (!bDownHeld)
	{
		// 倒れた最終姿勢で止め、UI表示後もAnimBP（Idle等）へ戻しません。
		const float HoldAt = DownMontage->GetPlayLength() - DownHoldBeforeEnd;
		if (Anim->Montage_IsActive(DownMontage) && Anim->Montage_GetPosition(DownMontage) >= HoldAt)
		{
			Anim->Montage_Pause(DownMontage);
			bDownHeld = true;
		}
	}
}

void AGameOverPresentationDirector::BeginGadgetGlowDecay(float DecayTime)
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (bGlowActive || !Player || !GadgetGlowMaterial)
		return;

	bGlowActive = true;
	GlowAge = 0.f;
	GlowDecay = FMath::Max(GameOverPresentation::MinGlowDecay, DecayTime);

	GadgetGlowMID = UMaterialInstanceDynamic::Create(GadgetGlowMaterial, this);
	GadgetGlowMID->SetVectorParameterValue(TEXT("GlowColor"), GadgetGlowColor);
	GadgetGlowMID->SetScalarParameterValue(TEXT("GlowAmount"), 0.f);

	// Playerに付いているガジェット（Attached Actor）の、表示中のMeshすべてへOverlayを掛ける。
	TArray<AActor*> Attached;
	Player->GetAttachedActors(Attached, true, true);

	for (AActor* A : Attached)
	{
		TArray<UMeshComponent*> Meshes;
		A->GetComponents<UMeshComponent>(Meshes);

		for (UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame)
				continue;

			GadgetMeshes.Add({Mesh, Mesh->GetOverlayMaterial()});
			Mesh->SetOverlayMaterial(GadgetGlowMID);
		}
	}
}

void AGameOverPresentationDirector::UpdateGadgetGlow(float Dt)
{
	using namespace GameOverPresentation;

	if (!GadgetGlowMID)
		return;

	GlowAge += Dt;

	if (GlowAge > GlowRiseTime + GlowDecay)
	{
		// 発光が消えたら元のOverlayへ戻します（電源が落ちた状態）。
		RestoreGadgetGlow();
		return;
	}

	// 短く立ち上がってから、指数カーブで減衰する。
	float Amount = GlowAge < GlowRiseTime
		? GlowAge / GlowRiseTime
		: FMath::Pow(1.f - (GlowAge - GlowRiseTime) / GlowDecay, GlowDecayExponent);

	// 後半は電源が不安定になったように短く明滅させます。
	const bool bFlickerPhase = GlowAge > GlowRiseTime + GlowDecay * GlowFlickerStartRatio;
	if (bFlickerPhase && FMath::Sin(GlowAge * GlowFlickerSpeed) < GlowFlickerThreshold)
	{
		Amount *= GlowFlickerDim;
	}

	GadgetGlowMID->SetScalarParameterValue(TEXT("GlowAmount"), FMath::Clamp(Amount, 0.f, 1.f));
}

void AGameOverPresentationDirector::RestoreGadgetGlow()
{
	for (const FGameOverGadgetMesh& M : GadgetMeshes)
	{
		if (M.Mesh.IsValid())
		{
			M.Mesh->SetOverlayMaterial(M.OriginalOverlay);
		}
	}

	GadgetMeshes.Reset();
	GadgetGlowMID = nullptr;
}

void AGameOverPresentationDirector::FadeOutHUD(float Duration)
{
	bHUDFading = true;
	HUDFadeAge = 0.f;
	HUDFadeDuration = FMath::Max(GameOverPresentation::MinFadeDuration, Duration);
}

void AGameOverPresentationDirector::BeginWarning(float FadeInTime)
{
	bWarning = true;
	WarningAge = 0.f;
	WarningFade = FMath::Max(GameOverPresentation::MinFadeDuration, FadeInTime);
	EnsureEffectWidget();
}

void AGameOverPresentationDirector::PlayNoiseBurst(float Duration, float Intensity)
{
	NoiseDuration = FMath::Max(GameOverPresentation::MinNoiseDuration, Duration);
	NoiseIntensity = Intensity;
	NoiseAge = 0.f;
	EnsureEffectWidget();
}

void AGameOverPresentationDirector::SetWarningAlphaForUI(float Alpha)
{
	// GameOver UIが表示されたら、フラッシュ・ノイズを止めて警告の濃さだけをUI側から操作する。
	UIWarningAlpha = Alpha;
	FlashPeak = 0.f;
	NoiseIntensity = 0.f;

	if (EffectWidget)
	{
		EffectWidget->SetFlashAlpha(0.f);
		EffectWidget->SetNoiseAmount(0.f);
		EffectWidget->SetWarningAlpha(Alpha);
		EffectWidget->SetVignetteAlpha(VignetteAlpha * Alpha);
	}
}

FVector AGameOverPresentationDirector::ComputeBodyFocus() const
{
	const ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player)
		return Focus;

	// pelvisとheadの中間（Boneが無ければMeshのBoundsの中心）を注視する。
	const USkeletalMeshComponent* Mesh = Player->GetMesh();
	static const FName Pelvis(TEXT("pelvis"));
	static const FName Head(TEXT("head"));

	if (Mesh && Mesh->GetBoneIndex(Pelvis) != INDEX_NONE && Mesh->GetBoneIndex(Head) != INDEX_NONE)
		return (Mesh->GetBoneLocation(Pelvis) + Mesh->GetBoneLocation(Head)) * .5f + FocusOffset;

	return (Mesh ? Mesh->Bounds.Origin : Player->GetActorLocation()) + FocusOffset;
}

void AGameOverPresentationDirector::BeginCameraPullBack(float Duration, float RiseDuration)
{
	using namespace GameOverPresentation;

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	APlayerController* PC = GetPresentationController();
	if (bCameraStarted || !Player || !PC)
		return;

	bCameraStarted = true;
	CameraAge = 0.f;
	CameraDuration = FMath::Max(MinCameraDuration, Duration);
	CameraRiseDuration = FMath::Max(MinCameraRiseDuration, RiseDuration);

	Arm = Player->FindComponentByClass<USpringArmComponent>();
	Camera = Player->FindComponentByClass<UCameraComponent>();

	if (!Arm.IsValid())
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("[GameOver] PlayerにSpringArmが無いため、カメラは動かしません。"));
		return;
	}

	// EndPlayで戻すため、演出前の値を控える。
	OrigArmLength = Arm->TargetArmLength;
	OrigTargetOffset = Arm->TargetOffset;
	OrigSocketOffset = Arm->SocketOffset;

	// 既存のCamera Lagはそのまま使い、判定Channel切替時の距離変化も滑らかにします。
	// 接触したBoarがCameraをBlockしてカメラが寄り切らないよう、演出中だけ判定Channelを替えます。
	OrigProbeChannel = Arm->ProbeChannel;
	Arm->ProbeChannel = CameraProbeChannel;

	if (Camera.IsValid())
	{
		OrigFOV = Camera->FieldOfView;
	}

	StartControlRotation = PC->GetControlRotation();
	StartControlRotation.Pitch = FRotator::NormalizeAxis(StartControlRotation.Pitch);
	Focus = Arm->GetComponentLocation() + OrigTargetOffset;

	// この演出はSpringArm / FollowCameraを動かすため、ACFの通常カメラが有効なら後退中はACFの出力を抑制して
	// 最終カメラのAuthorityを演出へ渡す（ACFは裏で評価を続ける）。解除はEndPlay。Directorが破棄されればACF側でも自動で外れる。
	if (UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(PC))
	{
		Cameras->SuppressOutput(this, ACFHandOffBlendTime);
		bSuppressedACF = true;
	}
}

void AGameOverPresentationDirector::UpdateCamera(float Dt)
{
	using namespace GameOverPresentation;

	APlayerController* PC = GetPresentationController();
	if (!Arm.IsValid() || !PC)
		return;

	CameraAge += Dt;
	const float T = FMath::Clamp(CameraAge / CameraDuration, 0.f, 1.f);
	const float Retreat = CameraCurve
		? FMath::Clamp(CameraCurve->GetFloatValue(T), 0.f, 1.f)
		: FMath::InterpEaseOut(0.f, 1.f, T, CameraEaseExponent);

	const float Rise = FMath::InterpEaseOut(0.f, 1.f,
		FMath::Clamp(CameraAge / CameraRiseDuration, 0.f, 1.f), CameraRiseEaseExponent);

	// 操作入力は遮断済みなので、演出がControl Rotationを直接動かします（急回転を避け、Yawはわずかに回り込むだけ）。
	FRotator Rot = StartControlRotation;
	Rot.Pitch = FMath::Lerp(StartControlRotation.Pitch, FMath::Min(CameraEndPitch, StartControlRotation.Pitch), Rise);
	Rot.Yaw = StartControlRotation.Yaw + CameraYawDrift * Retreat;
	PC->SetControlRotation(Rot);

	Arm->TargetArmLength = OrigArmLength + PullBackDistance * Retreat;
	Arm->SocketOffset = FMath::Lerp(OrigSocketOffset, FVector::ZeroVector, Rise);

	// 注視点は倒れていく身体（pelvisとheadの中間）へ滑らかに追従し、常に画面中央に捉えます。
	Focus = FMath::VInterpTo(Focus, ComputeBodyFocus(), Dt, FocusInterpSpeed);
	const FVector Origin = Arm->GetComponentLocation();
	FVector Wanted = FMath::Lerp(OrigTargetOffset, Focus - Origin, Rise);

	// Arm原点が壁の中に入らないよう、Capsule中心から注視点までをSweepして縮めます（後退側はSpringArm自身の衝突判定で回避）。
	if (ABoarPlayerCharacter* Player = GetPresentationPlayer())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GameOverCameraFocus), false, Player);
		TArray<AActor*> Attached;
		Player->GetAttachedActors(Attached, true);
		Params.AddIgnoredActors(Attached);

		FHitResult Hit;

		if (!Wanted.IsNearlyZero() && GetWorld()->SweepSingleByChannel(Hit, Origin, Origin + Wanted, FQuat::Identity,
			CameraProbeChannel, FCollisionShape::MakeSphere(FocusProbeRadius), Params))
		{
			Wanted *= FMath::Max(0.f, Hit.Time - FocusProbeBackOff);
		}
	}

	Arm->TargetOffset = Wanted;

	if (Camera.IsValid() && CameraEndFOV > 0.f)
	{
		Camera->SetFieldOfView(FMath::Lerp(OrigFOV, CameraEndFOV, Retreat));
	}
}

void AGameOverPresentationDirector::UpdateKnockback(float Dt)
{
	// 跳ね返りはヒットストップ明けから動きます。
	if (bHitStopActive)
		return;

	KnockAge += Dt;
	const float T = FMath::Clamp(KnockAge / KnockDuration, 0.f, 1.f);
	const float Move = FMath::InterpEaseOut(0.f, 1.f, T, GameOverPresentation::KnockbackEaseExponent);

	for (const FKnockback& K : Knockbacks)
	{
		if (AActor* A = K.Actor.Get())
		{
			// 水平に押し出しながら、Sinカーブで小さく跳ねる。
			// Sweepで壁・檻に当たったらそこで止まります（めり込み防止）。
			A->SetActorLocation(K.Start + K.Offset * Move + FVector(0, 0, KnockHop * FMath::Sin(PI * T)), true);
		}
	}

	if (T >= 1.f)
	{
		Knockbacks.Reset();
	}
}

void AGameOverPresentationDirector::UpdateEffects(float Dt)
{
	if (bHUDFading)
	{
		HUDFadeAge += Dt;
		const float Hud = 1.f - FMath::Clamp(HUDFadeAge / HUDFadeDuration, 0.f, 1.f);

		if (ABoarPlayerController* PC = GetPresentationController())
		{
			PC->SetPlayerHUDOpacity(Hud);

			if (Hud <= 0.f)
			{
				PC->SetPlayerHUDHidden(true);
				bHUDFading = false;
			}
		}
	}

	// GameOver UIが警告の濃さを操作し始めたら（UIWarningAlpha >= 0）、ここでは触らない。
	if (!EffectWidget || UIWarningAlpha >= 0.f)
		return;

	// 赤いフラッシュは2乗カーブで素早く消える。
	FlashAge += Dt;
	EffectWidget->SetFlashAlpha(FlashPeak * FMath::Square(1.f - FMath::Clamp(FlashAge / FlashDuration, 0.f, 1.f)));

	if (bWarning)
	{
		WarningAge += Dt;
		const float Warning = FMath::SmoothStep(0.f, 1.f, WarningAge / WarningFade);
		EffectWidget->SetWarningAlpha(Warning);
		EffectWidget->SetVignetteAlpha(VignetteAlpha * Warning);
	}

	NoiseAge += Dt;
	EffectWidget->SetNoiseAmount(NoiseAge <= NoiseDuration ? NoiseIntensity * (1.f - NoiseAge / NoiseDuration) : 0.f);
}

void AGameOverPresentationDirector::TickPresentation(float Dt)
{
	if (bHitStopActive)
	{
		HitStopRemaining -= Dt;

		if (HitStopRemaining <= 0.f)
		{
			EndHitStop();
		}
	}

	if (bWaitingForLanding)
	{
		ABoarPlayerCharacter* Player = GetPresentationPlayer();
		UCharacterMovementComponent* Move = Player ? Player->GetCharacterMovement() : nullptr;
		if (!Move)
		{
			bWaitingForLanding = false;
		}
		else if (Move->IsMovingOnGround() || GetPresentationAge() > GameOverPresentation::LandingWaitTimeout)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
			bWaitingForLanding = false;
		}
	}

	if (!Knockbacks.IsEmpty())
	{
		UpdateKnockback(Dt);
	}

	if (bDownStarted)
	{
		UpdateDown();
	}

	UpdateGadgetGlow(Dt);

	if (bCameraStarted)
	{
		UpdateCamera(Dt);
	}

	UpdateEffects(Dt);
	UpdateCameraDarken(Dt);
}

void AGameOverPresentationDirector::BeginCameraDarken(float FadeInTime)
{
	if (bDarkenStarted)
		return;

	bDarkenStarted = true;
	DarkenAge = 0.f;
	DarkenFade = FMath::Max(GameOverPresentation::MinFadeDuration, FadeInTime > 0.f ? FadeInTime : CameraDarkenFadeIn);

	// カメラ（どのViewTargetでも）に効くUnbound Post Process。SpringArm演出・ACFどちらのカメラでも同じ見え方になる。
	DarkenPostProcess = NewObject<UPostProcessComponent>(this, NAME_None, RF_Transient);
	DarkenPostProcess->bUnbound = true;
	DarkenPostProcess->Priority = CameraDarkenPriority;
	DarkenPostProcess->BlendWeight = 0.f;

	FPostProcessSettings& S = DarkenPostProcess->Settings;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = CameraVignetteIntensity;
	S.bOverride_SceneColorTint = true;
	S.SceneColorTint = CameraDarkenTint;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(CameraDarkenSaturation, CameraDarkenSaturation, CameraDarkenSaturation, 1.f);

	DarkenPostProcess->RegisterComponent();
}

void AGameOverPresentationDirector::UpdateCameraDarken(float Dt)
{
	if (!bCameraDarken)
		return;

	if (!bDarkenStarted)
	{
		if (CameraDarkenDelay >= 0.f && GetPresentationAge() >= CameraDarkenDelay)
		{
			BeginCameraDarken();
		}

		return;
	}

	if (!DarkenPostProcess)
		return;

	DarkenAge += Dt;

	// 視界がゆっくり狭まって暗くなる（Ease In-Out）。GameOver UI表示中もこの暗さを保つ。
	DarkenPostProcess->BlendWeight = FMath::SmoothStep(0.f, 1.f, DarkenAge / DarkenFade);
}

void AGameOverPresentationDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	EndHitStop();
	RestoreGadgetGlow();

	// SpringArm・FollowCameraを演出前の値へ戻す。
	if (Arm.IsValid() && bCameraStarted)
	{
		Arm->TargetArmLength = OrigArmLength;
		Arm->TargetOffset = OrigTargetOffset;
		Arm->SocketOffset = OrigSocketOffset;
		Arm->ProbeChannel = OrigProbeChannel;

		if (Camera.IsValid())
		{
			Camera->SetFieldOfView(OrigFOV);
		}
	}

	if (bSuppressedACF)
	{
		bSuppressedACF = false;

		if (UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(GetPresentationController()))
		{
			Cameras->ReleaseOutputSuppression(this, 0.f);
		}
	}

	if (ABoarPlayerCharacter* Player = GetPresentationPlayer())
	{
		if (bSingleNode)
		{
			Player->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}
		else if (UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance(); Anim && DownMontage && bDownStarted)
		{
			Anim->Montage_Stop(GameOverPresentation::StopDownMontageBlend, DownMontage);
		}
	}

	if (ABoarPlayerController* PC = GetPresentationController())
	{
		PC->SetPlayerHUDOpacity(1.f);
	}

	if (EffectWidget)
	{
		EffectWidget->RemoveFromParent();
	}

	EffectWidget = nullptr;

	// Retry・Level遷移で暗転を残さない。
	if (DarkenPostProcess)
	{
		DarkenPostProcess->BlendWeight = 0.f;
		DarkenPostProcess->DestroyComponent();
		DarkenPostProcess = nullptr;
	}

	Super::EndPlay(Reason);
}
