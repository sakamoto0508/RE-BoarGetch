#include "Presentation/GameOverPresentationDirector.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Boar/BoarBase.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
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

UBoarGameOverEffectWidget* AGameOverPresentationDirector::EnsureEffectWidget()
{
	if (EffectWidget) return EffectWidget;
	ABoarPlayerController* PC = GetPresentationController();
	if (!PC) return nullptr;
	// 画面効果Widget（HUDより前、GameOver UIより後ろ）。
	const TSubclassOf<UBoarGameOverEffectWidget> WidgetClass = EffectWidgetClass ? EffectWidgetClass : TSubclassOf<UBoarGameOverEffectWidget>(UBoarGameOverEffectWidget::StaticClass());
	EffectWidget = CreateWidget<UBoarGameOverEffectWidget>(PC, WidgetClass);
	if (EffectWidget) EffectWidget->AddToViewport(50);
	return EffectWidget;
}

void AGameOverPresentationDirector::BeginRedFlash(float PeakAlpha, float Duration)
{
	FlashPeak = PeakAlpha;
	FlashDuration = FMath::Max(.02f, Duration);
	FlashAge = 0.f;
	if (UBoarGameOverEffectWidget* W = EnsureEffectWidget()) W->SetFlashAlpha(FlashPeak);
}

void AGameOverPresentationDirector::BeginHitStop(float Duration, float TimeScale)
{
	if (Duration <= 0.f || bHitStopActive) return;
	OriginalTimeDilation = UGameplayStatics::GetGlobalTimeDilation(this);
	const float Scale = FMath::Clamp(TimeScale, .01f, 1.f);
	UGameplayStatics::SetGlobalTimeDilation(this, OriginalTimeDilation * Scale);
	// 同じフレーム内の後続Delayも実時間で進むよう、即座に打ち消します。
	CustomTimeDilation = 1.f / FMath::Max(OriginalTimeDilation * Scale, .0001f);
	HitStopRemaining = Duration;
	bHitStopActive = true;
}

void AGameOverPresentationDirector::EndHitStop()
{
	if (!bHitStopActive) return;
	bHitStopActive = false;
	if (GetWorld()) UGameplayStatics::SetGlobalTimeDilation(this, OriginalTimeDilation);
}

void AGameOverPresentationDirector::SettlePlayerOnGround()
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	UCharacterMovementComponent* Move = Player ? Player->GetCharacterMovement() : nullptr;
	if (!Move) return;
	const float Half = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GameOverGround), false, Player);
	const FVector From = Player->GetActorLocation();
	if (!GetWorld()->LineTraceTestByChannel(From, From - FVector(0, 0, Half + 12.f), ECC_Visibility, Params))
	{
		Move->SetMovementMode(MOVE_Falling);
		Move->Velocity = FVector(0, 0, FMath::Min(0.f, Move->Velocity.Z));
		bWaitingForLanding = true;
	}
}

void AGameOverPresentationDirector::KnockBackNearbyBoars(float Radius, float Distance, float HopHeight, float Duration)
{
	Knockbacks.Reset();
	KnockAge = 0.f;
	KnockDuration = FMath::Max(.05f, Duration);
	KnockHop = HopHeight;
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	const APlayerController* PC = GetPresentationController();
	if (!Player || Radius <= 0.f || Distance <= 0.f) return;
	const FVector P = Player->GetActorLocation();
	const FVector ToCamera = (PC && PC->PlayerCameraManager) ? (PC->PlayerCameraManager->GetCameraLocation() - P).GetSafeNormal2D() : FVector::ZeroVector;
	for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
	{
		if (It->IsCaptured() || FVector::Dist2D(It->GetActorLocation(), P) > Radius) continue;
		FVector Dir = (It->GetActorLocation() - P).GetSafeNormal2D();
		if (Dir.IsNearlyZero()) Dir = -Player->GetActorForwardVector().GetSafeNormal2D();
		// カメラとプレイヤーの間にいるBoarは横へ逃がし、ダウンの見え方を遮らないようにします。
		if (!ToCamera.IsNearlyZero() && FVector::DotProduct(Dir, ToCamera) > .3f)
		{
			const FVector Side = FVector::CrossProduct(FVector::UpVector, ToCamera);
			Dir = (Side * (FVector::DotProduct(Dir, Side) >= 0.f ? 1.f : -1.f) - ToCamera * .3f).GetSafeNormal2D();
		}
		Knockbacks.Add({ *It, It->GetActorLocation(), Dir * Distance });
	}
}

float AGameOverPresentationDirector::PlayDownAnimation()
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (bDownStarted || !Player) return 0.f;
	bDownStarted = true;
	USkeletalMeshComponent* Mesh = Player->GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const float Rate = FMath::Max(.1f, DownPlayRate);
	// 被弾時の再生中Montage（ガジェット等）を止めてから、FullBody Slotでダウンを再生します。
	if (Anim) Anim->Montage_Stop(.08f);
	if (Anim && DownMontage && Anim->Montage_Play(DownMontage, Rate) > 0.f) return DownMontage->GetPlayLength() / Rate;
	if (Mesh && DownAnimation && bAllowSingleNodeFallback)
	{
		Mesh->PlayAnimation(DownAnimation, false);
		Mesh->SetPlayRate(Rate);
		bSingleNode = bDownHeld = true;
		return DownAnimation->GetPlayLength() / Rate;
	}
	UE_LOG(LogTemp, Warning, TEXT("[GameOver] Down animation is not configured; continuing with camera/UI only."));
	return 0.f;
}

void AGameOverPresentationDirector::UpdateDown()
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player || !DownMontage || bSingleNode) return;
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	if (!Anim) return;
	if (bAllowSingleNodeFallback && ++MontageCheckFrames == 2)
	{
		// Slotが現在のAnimGraph経路に無いとMontageは見た目に反映されません。その場合は同じアニメをSingle Nodeで再生します。
		const FName Slot = DownMontage->SlotAnimTracks.IsEmpty() ? NAME_None : DownMontage->SlotAnimTracks[0].SlotName;
		UAnimSequenceBase* Sequence = DownAnimation.Get();
		if (!Sequence && !DownMontage->SlotAnimTracks.IsEmpty() && !DownMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
			Sequence = DownMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
		if (Sequence && Anim->GetSlotNodeGlobalWeight(Slot) <= KINDA_SMALL_NUMBER)
		{
			const float Position = Anim->Montage_GetPosition(DownMontage);
			Anim->Montage_Stop(0.f, DownMontage);
			Player->GetMesh()->PlayAnimation(Sequence, false);
			Player->GetMesh()->SetPosition(Position, false);
			Player->GetMesh()->SetPlayRate(FMath::Max(.1f, DownPlayRate));
			bSingleNode = bDownHeld = true;
			UE_LOG(LogTemp, Log, TEXT("[GameOver] Slot '%s' is not active in the AnimGraph; playing %s as single node."), *Slot.ToString(), *GetNameSafe(Sequence));
			return;
		}
	}
	if (!bDownHeld)
	{
		// 倒れた最終姿勢で止め、UI表示後もAnimBP（Idle等）へ戻しません。
		const float HoldAt = DownMontage->GetPlayLength() - .04f;
		if (Anim->Montage_IsActive(DownMontage) && Anim->Montage_GetPosition(DownMontage) >= HoldAt) { Anim->Montage_Pause(DownMontage); bDownHeld = true; }
	}
}

void AGameOverPresentationDirector::BeginGadgetGlowDecay(float DecayTime)
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (bGlowActive || !Player || !GadgetGlowMaterial) return;
	bGlowActive = true;
	GlowAge = 0.f;
	GlowDecay = FMath::Max(.05f, DecayTime);
	GadgetGlowMID = UMaterialInstanceDynamic::Create(GadgetGlowMaterial, this);
	GadgetGlowMID->SetVectorParameterValue(TEXT("GlowColor"), GadgetGlowColor);
	GadgetGlowMID->SetScalarParameterValue(TEXT("GlowAmount"), 0.f);
	TArray<AActor*> Attached;
	Player->GetAttachedActors(Attached, true, true);
	for (AActor* A : Attached)
	{
		TArray<UMeshComponent*> Meshes;
		A->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame) continue;
			GadgetMeshes.Add({ Mesh, Mesh->GetOverlayMaterial() });
			Mesh->SetOverlayMaterial(GadgetGlowMID);
		}
	}
}

void AGameOverPresentationDirector::UpdateGadgetGlow(float Dt)
{
	if (!GadgetGlowMID) return;
	GlowAge += Dt;
	const float Rise = .07f;
	if (GlowAge > Rise + GlowDecay)
	{
		// 発光が消えたら元のOverlayへ戻します（電源が落ちた状態）。
		RestoreGadgetGlow();
		return;
	}
	float Amount = GlowAge < Rise ? GlowAge / Rise : FMath::Pow(1.f - (GlowAge - Rise) / GlowDecay, 1.6f);
	// 後半は電源が不安定になったように短く明滅させます。
	if (GlowAge > Rise + GlowDecay * .35f && FMath::Sin(GlowAge * 47.f) < -.25f) Amount *= .3f;
	GadgetGlowMID->SetScalarParameterValue(TEXT("GlowAmount"), FMath::Clamp(Amount, 0.f, 1.f));
}

void AGameOverPresentationDirector::RestoreGadgetGlow()
{
	for (const FGameOverGadgetMesh& M : GadgetMeshes) if (M.Mesh.IsValid()) M.Mesh->SetOverlayMaterial(M.OriginalOverlay);
	GadgetMeshes.Reset();
	GadgetGlowMID = nullptr;
}

void AGameOverPresentationDirector::FadeOutHUD(float Duration)
{
	bHUDFading = true;
	HUDFadeAge = 0.f;
	HUDFadeDuration = FMath::Max(.01f, Duration);
}

void AGameOverPresentationDirector::BeginWarning(float FadeInTime)
{
	bWarning = true;
	WarningAge = 0.f;
	WarningFade = FMath::Max(.01f, FadeInTime);
	EnsureEffectWidget();
}

void AGameOverPresentationDirector::PlayNoiseBurst(float Duration, float Intensity)
{
	NoiseDuration = FMath::Max(.02f, Duration);
	NoiseIntensity = Intensity;
	NoiseAge = 0.f;
	EnsureEffectWidget();
}

void AGameOverPresentationDirector::SetWarningAlphaForUI(float Alpha)
{
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
	if (!Player) return Focus;
	const USkeletalMeshComponent* Mesh = Player->GetMesh();
	static const FName Pelvis(TEXT("pelvis")), Head(TEXT("head"));
	if (Mesh && Mesh->GetBoneIndex(Pelvis) != INDEX_NONE && Mesh->GetBoneIndex(Head) != INDEX_NONE)
		return (Mesh->GetBoneLocation(Pelvis) + Mesh->GetBoneLocation(Head)) * .5f + FocusOffset;
	return (Mesh ? Mesh->Bounds.Origin : Player->GetActorLocation()) + FocusOffset;
}

void AGameOverPresentationDirector::BeginCameraPullBack(float Duration, float RiseDuration)
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	APlayerController* PC = GetPresentationController();
	if (bCameraStarted || !Player || !PC) return;
	bCameraStarted = true;
	CameraAge = 0.f;
	CameraDuration = FMath::Max(.1f, Duration);
	CameraRiseDuration = FMath::Max(.05f, RiseDuration);
	Arm = Player->FindComponentByClass<USpringArmComponent>();
	Camera = Player->FindComponentByClass<UCameraComponent>();
	if (!Arm.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("[GameOver] Player has no SpringArm; camera stays.")); return; }
	OrigArmLength = Arm->TargetArmLength;
	OrigTargetOffset = Arm->TargetOffset;
	OrigSocketOffset = Arm->SocketOffset;
	// 既存のCamera Lagはそのまま使い、判定Channel切替時の距離変化も滑らかにします。
	// 接触したBoarがCameraをBlockしてカメラが寄り切らないよう、演出中だけ判定Channelを替えます。
	OrigProbeChannel = Arm->ProbeChannel;
	Arm->ProbeChannel = CameraProbeChannel;
	if (Camera.IsValid()) OrigFOV = Camera->FieldOfView;
	StartControlRotation = PC->GetControlRotation();
	StartControlRotation.Pitch = FRotator::NormalizeAxis(StartControlRotation.Pitch);
	Focus = Arm->GetComponentLocation() + OrigTargetOffset;
}

void AGameOverPresentationDirector::UpdateCamera(float Dt)
{
	APlayerController* PC = GetPresentationController();
	if (!Arm.IsValid() || !PC) return;
	CameraAge += Dt;
	const float T = FMath::Clamp(CameraAge / CameraDuration, 0.f, 1.f);
	const float Retreat = CameraCurve ? FMath::Clamp(CameraCurve->GetFloatValue(T), 0.f, 1.f) : FMath::InterpEaseOut(0.f, 1.f, T, CameraEaseExponent);
	const float Rise = FMath::InterpEaseOut(0.f, 1.f, FMath::Clamp(CameraAge / CameraRiseDuration, 0.f, 1.f), 2.f);

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
		TArray<AActor*> Attached; Player->GetAttachedActors(Attached, true);
		Params.AddIgnoredActors(Attached);
		FHitResult Hit;
		if (!Wanted.IsNearlyZero() && GetWorld()->SweepSingleByChannel(Hit, Origin, Origin + Wanted, FQuat::Identity, CameraProbeChannel, FCollisionShape::MakeSphere(12.f), Params))
			Wanted *= FMath::Max(0.f, Hit.Time - .05f);
	}
	Arm->TargetOffset = Wanted;
	if (Camera.IsValid() && CameraEndFOV > 0.f) Camera->SetFieldOfView(FMath::Lerp(OrigFOV, CameraEndFOV, Retreat));
}

void AGameOverPresentationDirector::UpdateKnockback(float Dt)
{
	if (bHitStopActive) return;  // 跳ね返りはヒットストップ明けから動きます。
	KnockAge += Dt;
	const float T = FMath::Clamp(KnockAge / KnockDuration, 0.f, 1.f);
	const float Move = FMath::InterpEaseOut(0.f, 1.f, T, 2.f);
	for (const FKnockback& K : Knockbacks)
		if (AActor* A = K.Actor.Get())
		{
			// Sweepで壁・檻に当たったらそこで止まります（めり込み防止）。
			A->SetActorLocation(K.Start + K.Offset * Move + FVector(0, 0, KnockHop * FMath::Sin(PI * T)), true);
		}
	if (T >= 1.f) Knockbacks.Reset();
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
			if (Hud <= 0.f) { PC->SetPlayerHUDHidden(true); bHUDFading = false; }
		}
	}
	if (!EffectWidget || UIWarningAlpha >= 0.f) return;
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
	if (bHitStopActive && (HitStopRemaining -= Dt) <= 0.f) EndHitStop();
	if (bWaitingForLanding)
	{
		ABoarPlayerCharacter* Player = GetPresentationPlayer();
		UCharacterMovementComponent* Move = Player ? Player->GetCharacterMovement() : nullptr;
		if (!Move) bWaitingForLanding = false;
		else if (Move->IsMovingOnGround() || GetPresentationAge() > 3.f)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
			bWaitingForLanding = false;
		}
	}
	if (!Knockbacks.IsEmpty()) UpdateKnockback(Dt);
	if (bDownStarted) UpdateDown();
	UpdateGadgetGlow(Dt);
	if (bCameraStarted) UpdateCamera(Dt);
	UpdateEffects(Dt);
}

void AGameOverPresentationDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	EndHitStop();
	RestoreGadgetGlow();
	if (Arm.IsValid() && bCameraStarted)
	{
		Arm->TargetArmLength = OrigArmLength;
		Arm->TargetOffset = OrigTargetOffset;
		Arm->SocketOffset = OrigSocketOffset;
		Arm->ProbeChannel = OrigProbeChannel;
		if (Camera.IsValid()) Camera->SetFieldOfView(OrigFOV);
	}
	if (ABoarPlayerCharacter* Player = GetPresentationPlayer())
	{
		if (bSingleNode) Player->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		else if (UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance(); Anim && DownMontage && bDownStarted) Anim->Montage_Stop(.1f, DownMontage);
	}
	if (ABoarPlayerController* PC = GetPresentationController()) PC->SetPlayerHUDOpacity(1.f);
	if (EffectWidget) EffectWidget->RemoveFromParent();
	EffectWidget = nullptr;
	Super::EndPlay(Reason);
}
