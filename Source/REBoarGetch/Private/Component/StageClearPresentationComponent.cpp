#include "Component/StageClearPresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Component/CapturePresentationComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/BoarGameMode.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Player/BoarPlayerCharacter.h"
#include "Player/BoarPlayerController.h"

UStageClearPresentationComponent::UStageClearPresentationComponent()
{
 PrimaryComponentTick.bCanEverTick = true;
 PrimaryComponentTick.bStartWithTickEnabled = false;
 StarColors = { FLinearColor(1.f, .82f, .08f), FLinearColor(1.f, 1.f, 1.f), FLinearColor(.05f, .85f, 1.f) };
}

void UStageClearPresentationComponent::BeginPlay()
{
 Super::BeginPlay();
 SetComponentTickEnabled(false);
}

void UStageClearPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 CancelPresentation();
 Super::EndPlay(Reason);
}

ABoarPlayerCharacter* UStageClearPresentationComponent::GetPlayer() const
{
 const APlayerController* PC = Cast<APlayerController>(GetOwner());
 return PC ? Cast<ABoarPlayerCharacter>(PC->GetPawn()) : nullptr;
}

void UStageClearPresentationComponent::PlaySound(USoundBase* Sound) const
{
 if (Sound && GetWorld() && GetWorld()->GetGameInstance())
  if (auto* Audio = GetWorld()->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlaySoundEffect2D(const_cast<UStageClearPresentationComponent*>(this), Sound);
}

bool UStageClearPresentationComponent::StartPresentation()
{
 // 多重起動防止：1回のStageで開始できるのは1度だけです。
 if (!bEnabled || Phase != EStageClearPresentationPhase::Idle) return false;
 const APlayerController* PC = Cast<APlayerController>(GetOwner());
 if (!PC || !PC->IsLocalController() || !GetPlayer()) return false;
 Phase = EStageClearPresentationPhase::WaitingCapture;
 PhaseAge = TotalAge = 0.f;
 SetComponentTickEnabled(true);
 UE_LOG(LogTemp, Log, TEXT("[StageClear] Presentation requested"));
 return true;
}

void UStageClearPresentationComponent::CollectMeshes()
{
 HiddenMeshes.Reset();
 ABoarPlayerCharacter* Player = GetPlayer();
 if (!Player) return;
 TArray<AActor*> Actors = { Player };
 // 網などの装備はMesh Socketへ子Actorとしてアタッチされています。孫Actorまで辿り、網だけ残らないようにします。
 for (int32 I = 0; I < Actors.Num(); ++I)
 {
  TArray<AActor*> Attached;
  Actors[I]->GetAttachedActors(Attached, false);
  for (AActor* A : Attached) Actors.AddUnique(A);
 }
 for (AActor* A : Actors)
 {
  TArray<UMeshComponent*> Meshes;
  A->GetComponents<UMeshComponent>(Meshes);
  for (UMeshComponent* Mesh : Meshes)
  {
   if (!Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame || (!Cast<USkeletalMeshComponent>(Mesh) && !Cast<UStaticMeshComponent>(Mesh))) continue;
   FStageClearHiddenMesh Entry;
   Entry.Source = Mesh;
   Entry.bOriginalVisible = Mesh->IsVisible();
   Entry.bOriginalHiddenInGame = Mesh->bHiddenInGame;
   Entry.OriginalOverlay = Mesh->GetOverlayMaterial();
   HiddenMeshes.Add(Entry);
  }
 }
}

void UStageClearPresentationComponent::BeginVictory()
{
 ABoarPlayerCharacter* Player = GetPlayer();
 APlayerController* PC = Cast<APlayerController>(GetOwner());
 if (!Player || !PC) { BeginFadeOut(); return; }
 Phase = EStageClearPresentationPhase::Victory;
 VictoryAge = -MontageStartDelay;
 if (auto* BoarPC = Cast<ABoarPlayerController>(PC)) BoarPC->SetPlayerHUDHidden(true);
 if (UGameInstance* GI = GetWorld()->GetGameInstance())
  if (auto* Audio = GI->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->StopBGM(StageBGMFadeOutTime);
 CollectMeshes();
 MeshOriginalRelativeLocation = Player->GetMesh()->GetRelativeLocation();

 // 消失地点は開始時に固定し、以後プレイヤーやボーンの動きに追従させません。
 const float HalfHeight = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
 VictoryFocusPoint = Player->GetActorLocation() - FVector(0, 0, HalfHeight) + VictoryFocusOffset;

 // 遮蔽物の少ない向きを選び、カメラが壁へめり込まないようにします。
 FCollisionQueryParams Params(SCENE_QUERY_STAT(StageClearCamera), false, Player);
 TArray<AActor*> Ignored; Player->GetAttachedActors(Ignored, true);
 Params.AddIgnoredActors(Ignored);
 const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
 const float Candidates[] = { CameraYawOffset, -CameraYawOffset, CameraYawOffset + 45.f, -CameraYawOffset - 45.f, 0.f };
 float BestFraction = -1.f, BestScore = -1.f;
 const FVector CurrentCamera = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : VictoryFocusPoint;
 for (float Yaw : Candidates)
 {
  const FVector Dir = Forward.RotateAngleAxis(Yaw, FVector::UpVector);
  const FVector Target = VictoryFocusPoint + Dir * (CameraDistance + RetreatDistance) + FVector(0, 0, CameraHeight + RetreatHeight);
  FHitResult Hit;
  const bool bHit = GetWorld()->SweepSingleByChannel(Hit, VictoryFocusPoint, Target, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(15.f), Params);
  const float Fraction = bHit ? Hit.Time : 1.f;
  // Blendは直線補間なので、現在のカメラから開始位置までが通るかも評価します（檻などを突き抜けないように）。
  const FVector Start = VictoryFocusPoint + Dir * CameraDistance + FVector(0, 0, CameraHeight);
  const bool bBlendBlocked = GetWorld()->SweepTestByChannel(CurrentCamera, Start, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(10.f), Params);
  const float Score = Fraction + (bBlendBlocked ? 0.f : .5f);
  if (Score > BestScore + .05f) { BestScore = Score; BestFraction = Fraction; CameraDirection = Dir; }
  if (!bHit && !bBlendBlocked) break;
 }
 // 最終位置まで通らない場合は、通れる距離だけ後退します（近距離側は維持）。
 const float Full = FVector(CameraDistance + RetreatDistance, 0, CameraHeight + RetreatHeight).Size();
 const float Near = FVector(CameraDistance, 0, CameraHeight).Size();
 const float Allowed = FMath::Max(Near, Full * BestFraction - 20.f);
 const float Scale = Full > Near ? FMath::Clamp((Allowed - Near) / (Full - Near), 0.f, 1.f) : 1.f;
 MaxRetreatDistance = RetreatDistance * Scale;
 MaxRetreatHeight = RetreatHeight * Scale;

 FActorSpawnParameters SpawnParams; SpawnParams.ObjectFlags |= RF_Transient;
 SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 VictoryCamera = GetWorld()->SpawnActor<ACameraActor>(ComputeCameraLocation(0.f), (VictoryFocusPoint - ComputeCameraLocation(0.f)).Rotation(), SpawnParams);
 if (VictoryCamera)
 {
  VictoryCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
  VictoryCamera->GetCameraComponent()->SetFieldOfView(VictoryFOV);
  OriginalViewTarget = PC->GetViewTarget();
  PC->SetViewTargetWithBlend(VictoryCamera, CameraBlendTime, VTBlend_Cubic);
 }
 UE_LOG(LogTemp, Log, TEXT("[StageClear] Victory start Focus=%s RetreatScale=%.2f"), *VictoryFocusPoint.ToString(), Scale);
}

FVector UStageClearPresentationComponent::ComputeCameraLocation(float A) const
{
 return VictoryFocusPoint + CameraDirection * (CameraDistance + MaxRetreatDistance * A) + FVector(0, 0, CameraHeight + MaxRetreatHeight * A);
}

float UStageClearPresentationComponent::EvaluateRetreat(float T) const
{
 T = FMath::Clamp(T, 0.f, 1.f);
 if (RetreatCurve) return FMath::Clamp(RetreatCurve->GetFloatValue(T), 0.f, 1.f);
 return FMath::InterpEaseInOut(0.f, 1.f, T, RetreatEaseExponent);
}

void UStageClearPresentationComponent::UpdateCamera(float A)
{
 if (!VictoryCamera) return;
 const FVector Location = ComputeCameraLocation(A);
 VictoryCamera->SetActorLocationAndRotation(Location, (VictoryFocusPoint - Location).Rotation());
 VictoryCamera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(VictoryFOV, RetreatEndFOV, A));
}

void UStageClearPresentationComponent::SpawnStars()
{
 bStarsSpawned = true;
 PlaySound(StarSound);
 if (!StarVFX) return;
 // 網Meshの上端（掲げた先端）から放ちます。網が無い場合は頭上を使います。
 FVector Origin = VictoryFocusPoint + FVector(0, 0, 110);
 if (ABoarPlayerCharacter* Player = GetPlayer())
 {
  float BestZ = -BIG_NUMBER;
  for (const FStageClearHiddenMesh& M : HiddenMeshes)
   if (M.Source.IsValid() && M.Source->GetOwner() != Player)
   {
    const FBoxSphereBounds B = M.Source->Bounds;
    if (B.Origin.Z + B.BoxExtent.Z > BestZ) { BestZ = B.Origin.Z + B.BoxExtent.Z; Origin = FVector(B.Origin.X, B.Origin.Y, BestZ); }
   }
 }
 Origin += StarOffset;
 for (int32 I = 0; I < StarColors.Num(); ++I)
 {
  UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, StarVFX, Origin, FRotator(0, 120.f * I, 0), FVector(StarScale), true, true, ENCPoolMethod::None, true);
  if (!C) continue;
  C->SetVariableLinearColor(TEXT("User.Color"), StarColors[I]);
  C->SetVariableFloat(TEXT("User.Seed"), float(I));
  StarComponents.Add(C);
 }
}

void UStageClearPresentationComponent::BeginGlow()
{
 bGlowStarted = true;
 if (!GlowMaterial) return;
 GlowMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
 GlowMID->SetVectorParameterValue(TEXT("GlowColor"), DataColor);
 GlowMID->SetScalarParameterValue(TEXT("GlowAmount"), .1f);
 for (FStageClearHiddenMesh& M : HiddenMeshes)
  if (auto* Mesh = Cast<UMeshComponent>(M.Source.Get())) Mesh->SetOverlayMaterial(GlowMID);
}

void UStageClearPresentationComponent::BeginDissolve()
{
 bDissolveStarted = true;
 PlaySound(TransferStartSound);
 FBox Box(ForceInit);
 for (const FStageClearHiddenMesh& M : HiddenMeshes) if (M.Source.IsValid()) Box += M.Source->Bounds.GetBox();
 if (!Box.IsValid) Box = FBox(VictoryFocusPoint - FVector(40, 40, 70), VictoryFocusPoint + FVector(40, 40, 110));
 DissolveMinZ = Box.Min.Z;
 DissolveHeight = FMath::Max(1.f, Box.Max.Z - Box.Min.Z);
 if (DataMaterial)
 {
  DataMID = UMaterialInstanceDynamic::Create(DataMaterial, this);
  DataMID->SetVectorParameterValue(TEXT("GlowColor"), DataColor);
  DataMID->SetScalarParameterValue(TEXT("CaptureMinZ"), DissolveMinZ);
  DataMID->SetScalarParameterValue(TEXT("CaptureHeight"), DissolveHeight);
  DataMID->SetScalarParameterValue(TEXT("DissolveProgress"), 0.f);
 }
 for (FStageClearHiddenMesh& M : HiddenMeshes)
 {
  UMeshComponent* Source = Cast<UMeshComponent>(M.Source.Get());
  if (!Source) continue;
  if (DataMID)
  {
   // 元Mesh・元Materialは変更せず、同じ姿勢の一時Proxyへデータ化Materialを掛けます。
   UMeshComponent* Proxy = nullptr;
   if (auto* Skel = Cast<USkeletalMeshComponent>(Source); Skel && Skel->GetSkeletalMeshAsset())
   {
    auto* P = NewObject<USkeletalMeshComponent>(Source->GetOwner(), NAME_None, RF_Transient);
    P->SetSkeletalMeshAsset(Skel->GetSkeletalMeshAsset());
    P->SetLeaderPoseComponent(Skel);
    Proxy = P;
   }
   else if (auto* Static = Cast<UStaticMeshComponent>(Source); Static && Static->GetStaticMesh())
   {
    auto* P = NewObject<UStaticMeshComponent>(Source->GetOwner(), NAME_None, RF_Transient);
    P->SetStaticMesh(Static->GetStaticMesh());
    Proxy = P;
   }
   if (Proxy)
   {
    Proxy->SetMobility(EComponentMobility::Movable);
    Proxy->SetupAttachment(Source->GetAttachParent(), Source->GetAttachSocketName());
    Proxy->SetRelativeTransform(Source->GetRelativeTransform());
    Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Proxy->SetGenerateOverlapEvents(false);
    Proxy->SetCanEverAffectNavigation(false);
    Proxy->SetCastShadow(false);
    for (int32 Slot = 0; Slot < FMath::Max(1, Source->GetNumMaterials()); ++Slot) Proxy->SetMaterial(Slot, DataMID);
    Proxy->RegisterComponent();
    if (!Source->GetAttachParent()) Proxy->SetWorldTransform(Source->GetComponentTransform());
    M.DataProxy = Proxy;
   }
  }
  Source->SetOverlayMaterial(M.OriginalOverlay);
  Source->SetVisibility(false, false);
 }
 // キューブ状のデータ粒子：スキャン線が通過した高さから順に舞い上がります。
 UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
 if (!Cube || !GlowMaterial) return;
 FragmentMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
 FragmentMID->SetVectorParameterValue(TEXT("GlowColor"), DataColor);
 FragmentMID->SetScalarParameterValue(TEXT("GlowStrength"), 9.f);
 FRandomStream Random(int32(VictoryFocusPoint.X * 13.f + VictoryFocusPoint.Y * 7.f));
 AActor* Owner = GetOwner();
 for (int32 I = 0; I < FMath::Clamp(FragmentCount, 0, 64); ++I)
 {
  auto* Piece = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
  Piece->SetMobility(EComponentMobility::Movable);
  Piece->SetStaticMesh(Cube);
  Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Piece->SetGenerateOverlapEvents(false);
  Piece->SetCanEverAffectNavigation(false);
  Piece->SetCastShadow(false);
  Piece->SetMaterial(0, FragmentMID);
  Piece->SetUsingAbsoluteLocation(true); Piece->SetUsingAbsoluteRotation(true); Piece->SetUsingAbsoluteScale(true);
  Piece->RegisterComponent();
  const float S = Random.FRandRange(.035f, .09f);
  Piece->SetWorldScale3D(FVector(S));
  Piece->SetWorldRotation(FRotator(Random.FRandRange(0, 90), Random.FRandRange(0, 90), 0));
  FStageClearFragment F; F.Mesh = Piece;
  F.Start = FVector(Random.FRandRange(Box.Min.X, Box.Max.X), Random.FRandRange(Box.Min.Y, Box.Max.Y), Random.FRandRange(Box.Min.Z, Box.Max.Z));
  F.Drift = FVector(Random.FRandRange(-30.f, 30.f), Random.FRandRange(-30.f, 30.f), FragmentRiseSpeed * Random.FRandRange(.6f, 1.4f));
  F.Delay = DissolveDuration * FMath::Clamp((F.Start.Z - DissolveMinZ) / DissolveHeight, 0.f, 1.f) * .9f;
  Piece->SetWorldLocation(F.Start);
  Piece->SetVisibility(false);
  Fragments.Add(F);
 }
 if (TransferVFX)
 {
  TransferComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TransferVFX, FVector(VictoryFocusPoint.X, VictoryFocusPoint.Y, DissolveMinZ + 2.f), FRotator::ZeroRotator, FVector(.7f), true, true, ENCPoolMethod::None, true);
  if (TransferComponent) TransferComponent->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(DataColor.R, DataColor.G, DataColor.B, .85f));
 }
}

void UStageClearPresentationComponent::UpdateDissolve(float P)
{
 if (DataMID)
 {
  DataMID->SetScalarParameterValue(TEXT("DissolveProgress"), FMath::Lerp(0.f, 1.02f, P));
  DataMID->SetScalarParameterValue(TEXT("GlowAmount"), 1.f + .5f * (1.f - P));
 }
}

void UStageClearPresentationComponent::FinishDissolve()
{
 bDissolveFinished = true;
 PlaySound(TransferCompleteSound);
 for (FStageClearHiddenMesh& M : HiddenMeshes)
  if (IsValid(M.DataProxy)) { M.DataProxy->DestroyComponent(); M.DataProxy = nullptr; }
 if (IsValid(TransferComponent)) TransferComponent->Deactivate();
 UE_LOG(LogTemp, Log, TEXT("[StageClear] Player and equipment vanished (%d meshes hidden)"), HiddenMeshes.Num());
}

void UStageClearPresentationComponent::BeginRetreat()
{
 Phase = EStageClearPresentationPhase::Retreat;
 RetreatAge = 0.f;
}

void UStageClearPresentationComponent::BeginFadeOut()
{
 Phase = EStageClearPresentationPhase::FadeOut;
 PhaseAge = 0.f;
 for (UNiagaraComponent* C : StarComponents) if (IsValid(C)) C->DestroyComponent();
 StarComponents.Reset();
 for (FStageClearFragment& F : Fragments) if (IsValid(F.Mesh)) F.Mesh->DestroyComponent();
 Fragments.Reset();
 if (IsValid(TransferComponent)) TransferComponent->DestroyComponent();
 TransferComponent = nullptr;
 if (auto* PC = Cast<APlayerController>(GetOwner()))
  if (PC->PlayerCameraManager) PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FMath::Max(0.01f, FadeOutDuration), FLinearColor::Black, false, true);
}

void UStageClearPresentationComponent::Finish()
{
 Phase = EStageClearPresentationPhase::Finished;
 SetComponentTickEnabled(false);
 UE_LOG(LogTemp, Log, TEXT("[StageClear] Presentation finished after %.2fs"), TotalAge);
 if (auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr) Mode->CompleteStageEndPresentation();
}

void UStageClearPresentationComponent::NotifyResultShown()
{
 if (!ResultBGM || !GetWorld() || !GetWorld()->GetGameInstance()) return;
 if (auto* Audio = GetWorld()->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlayBGM(this, ResultBGM, ResultBGMFadeInTime, true);
}

void UStageClearPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
 const float Dt = FMath::Min(DeltaTime, .1f);
 TotalAge += Dt; PhaseAge += Dt;
 ABoarPlayerCharacter* Player = GetPlayer();
 // 上限時間を越えた・Playerが消えた場合は残りを省略し、Resultへ進めます。
 if (Phase != EStageClearPresentationPhase::FadeOut && (TotalAge > SafetyTimeout || (!Player && Phase != EStageClearPresentationPhase::Retreat)))
 {
  UE_LOG(LogTemp, Warning, TEXT("[StageClear] Presentation shortened (timeout or missing player)."));
  if (Phase == EStageClearPresentationPhase::Victory && bDissolveStarted && !bDissolveFinished) FinishDissolve();
  BeginFadeOut();
 }
 switch (Phase)
 {
 case EStageClearPresentationPhase::WaitingCapture:
 {
  const auto* PC = Cast<ABoarPlayerController>(GetOwner());
  UCapturePresentationComponent* Capture = PC ? PC->CapturePresentation.Get() : nullptr;
  if (Capture && Capture->HasPendingCaptures() && PhaseAge < CaptureWaitTimeout) break;
  if (Capture && Capture->HasPendingCaptures()) Capture->CancelPresentation();
  BeginVictory();
  break;
 }
 case EStageClearPresentationPhase::Victory:
 {
  VictoryAge += Dt;
  if (VictoryAge >= 0.f && !bMontagePlaying && !bFallbackJump && Player)
  {
   UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
   const float Length = (Anim && VictoryMontage) ? Anim->Montage_Play(VictoryMontage, MontagePlayRate) : 0.f;
   bMontagePlaying = Length > 0.f;
   bFallbackJump = !bMontagePlaying;
   if (bFallbackJump) UE_LOG(LogTemp, Warning, TEXT("[StageClear] Victory montage unavailable; using fallback hop."));
  }
  if (bMontagePlaying && !bSingleNodePlayback && Player && bAllowSingleNodeFallback && ++MontageCheckFrames == 2)
  {
   // Slotが現在のAnimGraph経路に無いとMontageは見た目に反映されません。その場合は同じアニメをSingle Nodeで再生します。
   UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
   const FName Slot = VictoryMontage->SlotAnimTracks.IsEmpty() ? NAME_None : VictoryMontage->SlotAnimTracks[0].SlotName;
   UAnimSequenceBase* Sequence = VictoryAnimation ? VictoryAnimation.Get() : nullptr;
   if (!Sequence && !VictoryMontage->SlotAnimTracks.IsEmpty() && !VictoryMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
    Sequence = VictoryMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
   if (Anim && Sequence && Anim->GetSlotNodeGlobalWeight(Slot) <= KINDA_SMALL_NUMBER)
   {
    const float Position = Anim->Montage_GetPosition(VictoryMontage);
    Anim->Montage_Stop(0.f, VictoryMontage);
    Player->GetMesh()->PlayAnimation(Sequence, false);
    Player->GetMesh()->SetPosition(Position, false);
    Player->GetMesh()->SetPlayRate(MontagePlayRate);
    bSingleNodePlayback = true;
    bMontageHeld = true;  // 非ループのSingle Nodeは最終フレームで停止し、掲げた姿勢を保持します。
    UE_LOG(LogTemp, Log, TEXT("[StageClear] Slot '%s' is not active in the AnimGraph; playing %s as single node."), *Slot.ToString(), *GetNameSafe(Sequence));
   }
  }
  if (bMontagePlaying && !bMontageHeld && Player)
  {
   // 最終ポーズ（網を掲げた姿勢）で止め、消失が終わるまで通常AnimBPへ戻しません。
   UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
   const float HoldAt = VictoryMontage->GetPlayLength() - FMath::Max(.05f, VictoryMontage->BlendOut.GetBlendTime() + .05f);
   if (Anim && Anim->Montage_IsPlaying(VictoryMontage) && Anim->Montage_GetPosition(VictoryMontage) >= HoldAt) { Anim->Montage_Pause(VictoryMontage); bMontageHeld = true; }
  }
  if (bFallbackJump && Player)
  {
   const float H = FallbackJumpHeight * FMath::Sin(PI * FMath::Clamp((VictoryAge - JumpSoundTime) / .55f, 0.f, 1.f));
   Player->GetMesh()->SetRelativeLocation(MeshOriginalRelativeLocation + FVector(0, 0, H));
  }
  if (!bJinglePlayed && VictoryAge >= VictoryJingleTime)
  {
   bJinglePlayed = true;
   if (VictoryJingle && GetWorld()->GetGameInstance())
    if (auto* Audio = GetWorld()->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlayBGM(this, VictoryJingle, 0.f, true);
  }
  if (!bJumpSoundPlayed && VictoryAge >= JumpSoundTime) { bJumpSoundPlayed = true; PlaySound(JumpSound); }
  if (!bStarsSpawned && VictoryAge >= StarBurstTime) SpawnStars();
  if (!bGlowStarted && VictoryAge >= GlowStartTime) BeginGlow();
  if (GlowMID && !bDissolveStarted)
   GlowMID->SetScalarParameterValue(TEXT("GlowAmount"), FMath::Lerp(.1f, 1.f, FMath::SmoothStep(GlowStartTime, DissolveStartTime, VictoryAge)));
  if (!bDissolveStarted && VictoryAge >= DissolveStartTime) BeginDissolve();
  if (bDissolveStarted && !bDissolveFinished)
  {
   const float P = FMath::Clamp((VictoryAge - DissolveStartTime) / DissolveDuration, 0.f, 1.f);
   UpdateDissolve(P);
   if (P >= 1.f) FinishDissolve();
  }
  if (bDissolveFinished && VictoryAge >= DissolveStartTime + DissolveDuration + RetreatDelay) BeginRetreat();
  UpdateCamera(0.f);
  break;
 }
 case EStageClearPresentationPhase::Retreat:
 {
  RetreatAge += Dt; VictoryAge += Dt;
  UpdateCamera(EvaluateRetreat(RetreatAge / RetreatDuration));
  const float FadeStart = RetreatDuration - ParticleFadeDuration;
  const float ParticleAlpha = 1.f - FMath::Clamp((RetreatAge - FadeStart) / ParticleFadeDuration, 0.f, 1.f);
  if (FragmentMID) FragmentMID->SetScalarParameterValue(TEXT("GlowAmount"), .9f * ParticleAlpha);
  if (RetreatAge >= FadeStart)
   for (int32 I = 0; I < StarComponents.Num(); ++I)
    if (IsValid(StarComponents[I]))
    {
     StarComponents[I]->Deactivate();
     const FLinearColor C = StarColors.IsValidIndex(I) ? StarColors[I] : FLinearColor::White;
     StarComponents[I]->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(C.R, C.G, C.B, ParticleAlpha));
    }
  if (RetreatAge >= RetreatDuration) BeginFadeOut();
  break;
 }
 case EStageClearPresentationPhase::FadeOut:
  if (PhaseAge >= FadeOutDuration) Finish();
  break;
 default: break;
 }
 // 分解開始以降のデータ粒子：スキャン線が通過した順に出現し、上昇しながら消えます。
 if (bDissolveStarted && !Fragments.IsEmpty())
 {
  const float DataAge = VictoryAge - DissolveStartTime;
  for (FStageClearFragment& F : Fragments)
   if (IsValid(F.Mesh))
   {
    const float A = DataAge - F.Delay;
    F.Mesh->SetVisibility(A >= 0.f);
    if (A >= 0.f) F.Mesh->SetWorldLocation(F.Start + F.Drift * A);
   }
 }
}

void UStageClearPresentationComponent::RestoreVisuals()
{
 for (FStageClearHiddenMesh& M : HiddenMeshes)
 {
  if (IsValid(M.DataProxy)) M.DataProxy->DestroyComponent();
  if (auto* Mesh = Cast<UMeshComponent>(M.Source.Get()))
  {
   Mesh->SetOverlayMaterial(M.OriginalOverlay);
   Mesh->SetHiddenInGame(M.bOriginalHiddenInGame);
   Mesh->SetVisibility(M.bOriginalVisible, false);
  }
 }
 HiddenMeshes.Reset();
 if (ABoarPlayerCharacter* Player = GetPlayer())
 {
  if (bFallbackJump) Player->GetMesh()->SetRelativeLocation(MeshOriginalRelativeLocation);
  if (bSingleNodePlayback) Player->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
  else if (UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance(); Anim && VictoryMontage && bMontagePlaying) Anim->Montage_Stop(.1f, VictoryMontage);
 }
 for (UNiagaraComponent* C : StarComponents) if (IsValid(C)) C->DestroyComponent();
 StarComponents.Reset();
 for (FStageClearFragment& F : Fragments) if (IsValid(F.Mesh)) F.Mesh->DestroyComponent();
 Fragments.Reset();
 if (IsValid(TransferComponent)) TransferComponent->DestroyComponent();
 TransferComponent = nullptr;
}

void UStageClearPresentationComponent::RestoreCamera()
{
 if (auto* PC = Cast<APlayerController>(GetOwner()))
 {
  if (VictoryCamera && PC->GetViewTarget() == VictoryCamera)
   PC->SetViewTarget(OriginalViewTarget.IsValid() ? OriginalViewTarget.Get() : PC->GetPawn());
 }
 if (IsValid(VictoryCamera)) VictoryCamera->Destroy();
 VictoryCamera = nullptr;
}

void UStageClearPresentationComponent::CancelPresentation()
{
 if (Phase == EStageClearPresentationPhase::Idle) return;
 RestoreVisuals();
 RestoreCamera();
 SetComponentTickEnabled(false);
 Phase = EStageClearPresentationPhase::Idle;
 bJumpSoundPlayed = bJinglePlayed = bStarsSpawned = bGlowStarted = bDissolveStarted = bDissolveFinished = false;
 bMontagePlaying = bMontageHeld = bFallbackJump = bSingleNodePlayback = false;
 MontageCheckFrames = 0;
}
