#include "Component/GameOverPresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Boar/BoarBase.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/BoarGameMode.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/BoarPlayerCharacter.h"
#include "Player/BoarPlayerController.h"
#include "UI/BoarGameOverEffectWidget.h"

UGameOverPresentationComponent::UGameOverPresentationComponent()
{
 PrimaryComponentTick.bCanEverTick = true;
 PrimaryComponentTick.bStartWithTickEnabled = false;
 // 演出の時間はGetRealTimeSecondsの差分（実時間）で進め、ヒットストップの影響を受けません。
 NoiseBurstTimes = { .02f, 1.1f };
}

void UGameOverPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 CancelPresentation();
 Super::EndPlay(Reason);
}

ABoarPlayerCharacter* UGameOverPresentationComponent::GetPlayer() const
{
 const APlayerController* PC = Cast<APlayerController>(GetOwner());
 return PC ? Cast<ABoarPlayerCharacter>(PC->GetPawn()) : nullptr;
}

void UGameOverPresentationComponent::PlaySound(USoundBase* Sound, float Pitch) const
{
 if (Sound && GetWorld() && GetWorld()->GetGameInstance())
  if (auto* Audio = GetWorld()->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>())
   Audio->PlaySoundEffect2D(const_cast<UGameOverPresentationComponent*>(this), Sound, 1.f, Pitch);
}

bool UGameOverPresentationComponent::StartPresentation()
{
 // 多重起動防止：1回のGameOverで開始できるのは1度だけです。
 if (!bEnabled || Phase != EGameOverPresentationPhase::Idle) return false;
 auto* PC = Cast<ABoarPlayerController>(GetOwner());
 if (!PC || !PC->IsLocalController()) return false;
 Phase = EGameOverPresentationPhase::Playing;
 Age = 0.f;
 LastRealTime = GetWorld()->GetRealTimeSeconds();
 PlayedNoiseBursts.Reset();
 UE_LOG(LogTemp, Log, TEXT("[GameOver] Presentation start"));

 // 画面効果Widget（HUDより前、GameOver UIより後ろ）。
 TSubclassOf<UBoarGameOverEffectWidget> WidgetClass = EffectWidgetClass ? EffectWidgetClass : TSubclassOf<UBoarGameOverEffectWidget>(UBoarGameOverEffectWidget::StaticClass());
 EffectWidget = CreateWidget<UBoarGameOverEffectWidget>(PC, WidgetClass);
 if (EffectWidget)
 {
  EffectWidget->AddToViewport(50);
  EffectWidget->SetFlashAlpha(FlashPeakAlpha);
 }
 PlaySound(ImpactSound, ImpactPitch);
 if (UGameInstance* GI = GetWorld()->GetGameInstance())
  if (auto* Audio = GI->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->StopBGM(BGMFadeOutTime);
 BeginHitStop();

 // 空中でHP0になった場合は、着地するまで重力だけを有効にします（入力は遮断済み）。
 if (ABoarPlayerCharacter* Player = GetPlayer())
 {
  UCharacterMovementComponent* Move = Player->GetCharacterMovement();
  const float Half = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
  FCollisionQueryParams Params(SCENE_QUERY_STAT(GameOverGround), false, Player);
  const FVector From = Player->GetActorLocation();
  if (Move && !GetWorld()->LineTraceTestByChannel(From, From - FVector(0, 0, Half + 12.f), ECC_Visibility, Params))
  {
   Move->SetMovementMode(MOVE_Falling);
   Move->Velocity = FVector(0, 0, FMath::Min(0.f, Move->Velocity.Z));
   bWaitingForLanding = true;
  }
 }
 BeginKnockback();
 // ダウンアニメ開始時に実際の長さで更新します。アニメが無い場合はこの時刻でUIへ進みます。
 FinishTime = FMath::Max(FallbackFinishTime, CameraStartTime + CameraDuration + UIDelayAfterSettle);
 SetComponentTickEnabled(true);
 return true;
}

void UGameOverPresentationComponent::BeginHitStop()
{
 if (HitStopDuration <= 0.f) return;
 OriginalTimeDilation = UGameplayStatics::GetGlobalTimeDilation(this);
 UGameplayStatics::SetGlobalTimeDilation(this, OriginalTimeDilation * FMath::Clamp(HitStopTimeScale, .01f, 1.f));
 bHitStopActive = true;
}

void UGameOverPresentationComponent::EndHitStop()
{
 if (!bHitStopActive) return;
 bHitStopActive = false;
 if (GetWorld()) UGameplayStatics::SetGlobalTimeDilation(this, OriginalTimeDilation);
}

void UGameOverPresentationComponent::StartDownAnimation()
{
 bDownStarted = true;
 ABoarPlayerCharacter* Player = GetPlayer();
 if (!Player) return;
 USkeletalMeshComponent* Mesh = Player->GetMesh();
 UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
 const float Rate = FMath::Max(.1f, DownPlayRate);
 // 被弾時の再生中Montage（ガジェット等）を止めてから、FullBody Slotでダウンを再生します。
 if (Anim) Anim->Montage_Stop(.08f);
 const float Length = (Anim && DownMontage) ? Anim->Montage_Play(DownMontage, Rate) : 0.f;
 UAnimSequenceBase* Sequence = DownAnimation.Get();
 if (Length > 0.f) DownLength = DownMontage->GetPlayLength() / Rate;
 else if (Mesh && Sequence && bAllowSingleNodeFallback)
 {
  Mesh->PlayAnimation(Sequence, false);
  Mesh->SetPlayRate(Rate);
  bSingleNode = bDownHeld = true;
  DownLength = Sequence->GetPlayLength() / Rate;
 }
 else
 {
  UE_LOG(LogTemp, Warning, TEXT("[GameOver] Down animation is not configured; continuing with camera/UI only."));
  return;
 }
 // ダウン完了（最終姿勢）とカメラ停止の両方を待ってからUIを出します。
 FinishTime = FMath::Max(Age + DownLength, CameraStartTime + CameraDuration) + UIDelayAfterSettle;
}

void UGameOverPresentationComponent::UpdateDownAnimation()
{
 ABoarPlayerCharacter* Player = GetPlayer();
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
   Player->GetMesh()->SetPlayRate(DownPlayRate);
   bSingleNode = bDownHeld = true;
   UE_LOG(LogTemp, Log, TEXT("[GameOver] Slot '%s' is not active in the AnimGraph; playing %s as single node."), *Slot.ToString(), *GetNameSafe(Sequence));
   return;
  }
 }
 if (!bDownHeld)
 {
  // 倒れた最終姿勢で止め、UI表示後もAnimBP（Idle等）へ戻しません。
  // （Auto Blend Out無効のMontageは終端で止まりますが、明示的にPauseして保持を保証します。）
  const float HoldAt = DownMontage->GetPlayLength() - .04f;
  if (Anim->Montage_IsActive(DownMontage) && Anim->Montage_GetPosition(DownMontage) >= HoldAt) { Anim->Montage_Pause(DownMontage); bDownHeld = true; }
 }
}

FVector UGameOverPresentationComponent::ComputeBodyFocus() const
{
 const ABoarPlayerCharacter* Player = GetPlayer();
 if (!Player) return Focus;
 const USkeletalMeshComponent* Mesh = Player->GetMesh();
 static const FName Pelvis(TEXT("pelvis")), Head(TEXT("head"));
 if (Mesh && Mesh->GetBoneIndex(Pelvis) != INDEX_NONE && Mesh->GetBoneIndex(Head) != INDEX_NONE)
  return (Mesh->GetBoneLocation(Pelvis) + Mesh->GetBoneLocation(Head)) * .5f + FocusOffset;
 return (Mesh ? Mesh->Bounds.Origin : Player->GetActorLocation()) + FocusOffset;
}

void UGameOverPresentationComponent::BeginCamera()
{
 bCameraStarted = true;
 CameraAge = 0.f;
 ABoarPlayerCharacter* Player = GetPlayer();
 APlayerController* PC = Cast<APlayerController>(GetOwner());
 if (!Player || !PC) return;
 Arm = Player->FindComponentByClass<USpringArmComponent>();
 Camera = Player->FindComponentByClass<UCameraComponent>();
 if (!Arm.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("[GameOver] Player has no SpringArm; camera stays.")); return; }
 OrigArmLength = Arm->TargetArmLength;
 OrigTargetOffset = Arm->TargetOffset;
 OrigSocketOffset = Arm->SocketOffset;
 bOrigCameraLag = Arm->bEnableCameraLag;
 bOrigRotationLag = Arm->bEnableCameraRotationLag;
 // 既存のCamera Lagはそのまま使い、判定Channel切替時の距離変化も滑らかにします。
 // 接触したBoarがCameraをBlockしてカメラが寄り切らないよう、演出中だけ判定Channelを替えます。
 OrigProbeChannel = Arm->ProbeChannel;
 Arm->ProbeChannel = CameraProbeChannel;
 if (Camera.IsValid()) OrigFOV = Camera->FieldOfView;
 StartControlRotation = PC->GetControlRotation();
 StartControlRotation.Pitch = FRotator::NormalizeAxis(StartControlRotation.Pitch);
 Focus = Arm->GetComponentLocation() + OrigTargetOffset;
}

void UGameOverPresentationComponent::UpdateCamera(float Dt)
{
 APlayerController* PC = Cast<APlayerController>(GetOwner());
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
 if (ABoarPlayerCharacter* Player = GetPlayer())
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

void UGameOverPresentationComponent::BeginGadgetGlow()
{
 bGadgetGlowStarted = true;
 ABoarPlayerCharacter* Player = GetPlayer();
 if (!Player || !GadgetGlowMaterial) return;
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

void UGameOverPresentationComponent::UpdateGadgetGlow()
{
 if (!GadgetGlowMID) return;
 const float T = Age - GadgetGlowStartTime;
 const float Rise = .07f;
 if (T > Rise + GadgetGlowDecayTime)
 {
  // 発光が消えたら元のOverlayへ戻します（電源が落ちた状態）。
  for (const FGameOverGadgetMesh& M : GadgetMeshes) if (M.Mesh.IsValid()) M.Mesh->SetOverlayMaterial(M.OriginalOverlay);
  GadgetMeshes.Reset();
  GadgetGlowMID = nullptr;
  return;
 }
 float Amount = T < Rise ? T / Rise : FMath::Pow(1.f - (T - Rise) / GadgetGlowDecayTime, 1.6f);
 // 後半は電源が不安定になったように短く明滅させます。
 if (T > Rise + GadgetGlowDecayTime * .35f && FMath::Sin(T * 47.f) < -.25f) Amount *= .3f;
 GadgetGlowMID->SetScalarParameterValue(TEXT("GlowAmount"), FMath::Clamp(Amount, 0.f, 1.f));
}

void UGameOverPresentationComponent::UpdateEffects()
{
 if (auto* PC = Cast<ABoarPlayerController>(GetOwner()))
 {
  const float Hud = 1.f - FMath::Clamp((Age - HUDFadeStartTime) / HUDFadeOutTime, 0.f, 1.f);
  PC->SetPlayerHUDOpacity(Hud);
  if (Hud <= 0.f) PC->SetPlayerHUDHidden(true);
 }
 float Noise = 0.f;
 for (int32 I = 0; I < NoiseBurstTimes.Num(); ++I)
 {
  const float T = Age - NoiseBurstTimes[I];
  if (T < 0.f || T > NoiseBurstDuration) continue;
  if (!PlayedNoiseBursts.Contains(I)) { PlayedNoiseBursts.Add(I); PlaySound(NoiseSound); }
  Noise = FMath::Max(Noise, NoiseIntensity * (1.f - T / NoiseBurstDuration) * (I == 0 ? 1.f : .7f));
 }
 if (!EffectWidget) return;
 EffectWidget->SetFlashAlpha(FlashPeakAlpha * FMath::Square(1.f - FMath::Clamp(Age / FlashDuration, 0.f, 1.f)));
 const float Warning = FMath::SmoothStep(0.f, 1.f, (Age - WarningStartTime) / WarningFadeInTime);
 EffectWidget->SetWarningAlpha(Warning);
 EffectWidget->SetVignetteAlpha(VignetteAlpha * Warning);
 EffectWidget->SetNoiseAmount(Noise);
}

void UGameOverPresentationComponent::BeginKnockback()
{
 Knockbacks.Reset();
 ABoarPlayerCharacter* Player = GetPlayer();
 const APlayerController* PC = Cast<APlayerController>(GetOwner());
 if (!Player || KnockbackRadius <= 0.f || KnockbackDistance <= 0.f) return;
 const FVector P = Player->GetActorLocation();
 FVector ToCamera = (PC && PC->PlayerCameraManager) ? (PC->PlayerCameraManager->GetCameraLocation() - P).GetSafeNormal2D() : FVector::ZeroVector;
 for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
 {
  if (It->IsCaptured() || FVector::Dist2D(It->GetActorLocation(), P) > KnockbackRadius) continue;
  FVector Dir = (It->GetActorLocation() - P).GetSafeNormal2D();
  if (Dir.IsNearlyZero()) Dir = -Player->GetActorForwardVector().GetSafeNormal2D();
  // カメラとプレイヤーの間にいるBoarは横へ逃がし、ダウンの見え方を遮らないようにします。
  if (!ToCamera.IsNearlyZero() && FVector::DotProduct(Dir, ToCamera) > .3f)
  {
   const FVector Side = FVector::CrossProduct(FVector::UpVector, ToCamera);
   Dir = (Side * (FVector::DotProduct(Dir, Side) >= 0.f ? 1.f : -1.f) - ToCamera * .3f).GetSafeNormal2D();
  }
  Knockbacks.Add({ *It, It->GetActorLocation(), Dir * KnockbackDistance });
 }
}

void UGameOverPresentationComponent::UpdateKnockback()
{
 const float T = FMath::Clamp((Age - HitStopDuration) / KnockbackDuration, 0.f, 1.f);
 const float Move = FMath::InterpEaseOut(0.f, 1.f, T, 2.f);
 for (const FKnockback& K : Knockbacks)
  if (AActor* A = K.Actor.Get())
  {
   // Sweepで壁・檻に当たったらそこで止まります（めり込み防止）。
   const FVector Target = K.Start + K.Offset * Move + FVector(0, 0, KnockbackHopHeight * FMath::Sin(PI * T));
   A->SetActorLocation(Target, true);
  }
 if (T >= 1.f) Knockbacks.Reset();
}

void UGameOverPresentationComponent::UpdateLanding()
{
 ABoarPlayerCharacter* Player = GetPlayer();
 UCharacterMovementComponent* Move = Player ? Player->GetCharacterMovement() : nullptr;
 if (!Move) { bWaitingForLanding = false; return; }
 if (Move->IsMovingOnGround() || Age > SafetyTimeout * .5f)
 {
  Move->StopMovementImmediately();
  Move->DisableMovement();
  bWaitingForLanding = false;
 }
}

void UGameOverPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
 if (Phase != EGameOverPresentationPhase::Playing) return;
 const double Now = GetWorld()->GetRealTimeSeconds();
 const float Dt = FMath::Clamp(float(Now - LastRealTime), 0.f, .1f);
 LastRealTime = Now;
 Age += Dt;
 if (bHitStopActive && Age >= HitStopDuration) EndHitStop();
 if (bWaitingForLanding) UpdateLanding();
 if (!Knockbacks.IsEmpty()) UpdateKnockback();
 if (!bDownStarted && Age >= DownStartTime && !bHitStopActive) StartDownAnimation();
 if (bDownStarted) UpdateDownAnimation();
 if (!bGadgetGlowStarted && Age >= GadgetGlowStartTime) BeginGadgetGlow();
 UpdateGadgetGlow();
 if (!bSystemDownPlayed && Age >= SystemDownSoundTime) { bSystemDownPlayed = true; PlaySound(SystemDownSound); }
 if (bDownStarted && !bBodyImpactPlayed && Age >= DownStartTime + BodyImpactAnimTime / FMath::Max(.1f, DownPlayRate))
 {
  bBodyImpactPlayed = true;
  PlaySound(BodyFallSound);
 }
 if (!bCameraStarted && Age >= CameraStartTime) BeginCamera();
 if (bCameraStarted) UpdateCamera(Dt);
 UpdateEffects();
 // 上限時間・Player消失時も必ずUIへ進めます（Asset未設定でも無限待機しません）。
 if (Age >= FinishTime || Age >= SafetyTimeout || (!GetPlayer() && Age >= DownStartTime))
 {
  if (Age >= SafetyTimeout) UE_LOG(LogTemp, Warning, TEXT("[GameOver] Presentation timed out; showing GameOver UI."));
  Finish();
 }
}

void UGameOverPresentationComponent::Finish()
{
 if (Phase != EGameOverPresentationPhase::Playing) return;
 Phase = EGameOverPresentationPhase::Finished;
 SetComponentTickEnabled(false);
 EndHitStop();
 for (const FGameOverGadgetMesh& M : GadgetMeshes) if (M.Mesh.IsValid()) M.Mesh->SetOverlayMaterial(M.OriginalOverlay);
 GadgetMeshes.Reset();
 if (EffectWidget) { EffectWidget->SetFlashAlpha(0.f); EffectWidget->SetNoiseAmount(0.f); }
 UE_LOG(LogTemp, Log, TEXT("[GameOver] Presentation finished after %.2fs (down=%s held=%s singleNode=%s)"), Age,
  bDownStarted ? TEXT("yes") : TEXT("no"), bDownHeld ? TEXT("yes") : TEXT("no"), bSingleNode ? TEXT("yes") : TEXT("no"));
 if (auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr) Mode->CompleteStageEndPresentation();
}

void UGameOverPresentationComponent::NotifyGameOverUIShown()
{
 PlaySound(DefeatJingle);
 if (EffectWidget)
 {
  EffectWidget->SetWarningAlpha(WarningAlphaDuringUI);
  EffectWidget->SetVignetteAlpha(VignetteAlpha * WarningAlphaDuringUI);
 }
}

void UGameOverPresentationComponent::RestoreAll()
{
 EndHitStop();
 for (const FGameOverGadgetMesh& M : GadgetMeshes) if (M.Mesh.IsValid()) M.Mesh->SetOverlayMaterial(M.OriginalOverlay);
 GadgetMeshes.Reset();
 GadgetGlowMID = nullptr;
 if (Arm.IsValid() && bCameraStarted)
 {
  Arm->TargetArmLength = OrigArmLength;
  Arm->TargetOffset = OrigTargetOffset;
  Arm->SocketOffset = OrigSocketOffset;
  Arm->bEnableCameraLag = bOrigCameraLag;
  Arm->bEnableCameraRotationLag = bOrigRotationLag;
  Arm->ProbeChannel = OrigProbeChannel;
  if (Camera.IsValid()) Camera->SetFieldOfView(OrigFOV);
 }
 if (ABoarPlayerCharacter* Player = GetPlayer())
 {
  if (bSingleNode) Player->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
  else if (UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance(); Anim && DownMontage && bDownStarted) Anim->Montage_Stop(.1f, DownMontage);
 }
 if (auto* PC = Cast<ABoarPlayerController>(GetOwner())) PC->SetPlayerHUDOpacity(1.f);
 if (EffectWidget) EffectWidget->RemoveFromParent();
 EffectWidget = nullptr;
}

void UGameOverPresentationComponent::CancelPresentation()
{
 if (Phase == EGameOverPresentationPhase::Idle) return;
 RestoreAll();
 SetComponentTickEnabled(false);
 Phase = EGameOverPresentationPhase::Idle;
 Age = 0.f;
 bHitStopActive = bDownStarted = bDownHeld = bSingleNode = bBodyImpactPlayed = false;
 bCameraStarted = bGadgetGlowStarted = bSystemDownPlayed = bWaitingForLanding = false;
 MontageCheckFrames = 0;
 Knockbacks.Reset();
 Arm = nullptr; Camera = nullptr;
}
