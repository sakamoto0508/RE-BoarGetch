#include "Component/CapturePresentationComponent.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Boar/BoarBase.h"
#include "Core/BoarGameMode.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

UCaptureSuccessCameraShake::UCaptureSuccessCameraShake(const FObjectInitializer& ObjectInitializer)
 : Super(ObjectInitializer)
{
 bSingleInstance = true;
 auto* Pattern = CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("CaptureShake"));
 Pattern->Duration = .25f; Pattern->BlendInTime = .02f; Pattern->BlendOutTime = .09f;
 Pattern->LocationAmplitudeMultiplier = 0.f;
 Pattern->Pitch.Amplitude = .3f; Pattern->Yaw.Amplitude = .17f; Pattern->Roll.Amplitude = .12f;
 Pattern->Pitch.Frequency = Pattern->Yaw.Frequency = Pattern->Roll.Frequency = 24.f;
 Pattern->FOV.Amplitude = 0.f;
 SetRootShakePattern(Pattern);
}
UCapturePresentationComponent::UCapturePresentationComponent()
{
 PrimaryComponentTick.bCanEverTick = false;
 CameraShake = UCaptureSuccessCameraShake::StaticClass();
}
float UCapturePresentationComponent::GetTotalDuration() const
{
 return HitStopDuration + SlowDuration + CaptureDissolveDuration + TransferFinishDuration;
}
void UCapturePresentationComponent::BeginPlay()
{
 Super::BeginPlay();
 LastRealTime = GetWorld()->GetRealTimeSeconds();
 TickHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::OnWorldTick);
}
void UCapturePresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 FWorldDelegates::OnWorldTickStart.Remove(TickHandle);
 CancelPresentation();
 Super::EndPlay(Reason);
}
bool UCapturePresentationComponent::PresentCapturedBoar(ABoarBase* Boar, bool bSharedFeedback)
{
 auto* PC = Cast<APlayerController>(GetOwner());
 if (!bEnabled || !PC || !PC->IsLocalController() || !IsValid(Boar) || !Boar->IsCaptured()) return false;
 if (Targets.ContainsByPredicate([Boar](const FCaptureVisualTarget& T){return T.Boar==Boar;})) return true;
 if (Targets.IsEmpty()) LastRealTime = GetWorld()->GetRealTimeSeconds();
 FCaptureVisualTarget T; T.Boar = Boar; T.Mesh = Boar->GetMesh(); T.CaptureLocation = Boar->GetActorLocation();
 if (T.Mesh.IsValid()) T.bOriginalVisible = T.Mesh->IsVisible();
 if (T.Mesh.IsValid() && GlowMaterial)
 {
  const FBoxSphereBounds Bounds = T.Mesh->Bounds;
  T.CaptureMinZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
  T.CaptureHeight = FMath::Max(1.f, Bounds.BoxExtent.Z * 2.f);
  T.OriginalOverlay = T.Mesh->GetOverlayMaterial();
  T.Glow = UMaterialInstanceDynamic::Create(GlowMaterial,this);
  T.Glow->SetScalarParameterValue(TEXT("GlowAmount"),.15f);
  T.Mesh->SetOverlayMaterial(T.Glow);
 }
 if (CaptureVFX)
 {
  T.VFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,CaptureVFX,Boar->GetActorLocation()-FVector(0,0,35),FRotator::ZeroRotator,FVector(VFXScale),false,false);
  if (T.VFX)
  {
   T.VFX->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(.01f,.75f,1.f,.82f));
   T.VFX->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
   T.VFX->SetCanRenderWhileSeeking(true);
   T.VFX->Activate(true);
  }
 }
 Targets.Add(T);
 Boar->OnDestroyed.AddUniqueDynamic(this,&ThisClass::OnTargetDestroyed);
 if (bSharedFeedback && !bSharedActive)
 {
  bSharedActive = true; SharedAge = 0.f; bConfirmed = false; bTransferSoundPlayed = false;
  OriginalTimeScale = UGameplayStatics::GetGlobalTimeDilation(this);
  Camera = PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UCameraComponent>() : nullptr;
  if (Camera.IsValid()) { OriginalFOV = Camera->FieldOfView; bOwnCamera = true; }
  bOwnTimeScale = true;
  UGameplayStatics::SetGlobalTimeDilation(this,OriginalTimeScale * FMath::Clamp(HitStopDuration>0?HitStopTimeScale:SlowTimeScale,.001f,1.f));
  if (PC->PlayerCameraManager && CameraShake) ActiveShake = PC->PlayerCameraManager->StartCameraShake(CameraShake);
  PlaySound(ImpactSound);
 }
 return true;
}
void UCapturePresentationComponent::PlaySound(USoundBase* Sound)
{
 if (Sound && GetWorld() && GetWorld()->GetGameInstance())
  if (auto* Audio=GetWorld()->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlaySoundEffect2D(this,Sound);
}
void UCapturePresentationComponent::RestoreSharedState()
{
 if (bOwnTimeScale && GetWorld()) UGameplayStatics::SetGlobalTimeDilation(this,OriginalTimeScale);
 if (bOwnCamera && Camera.IsValid()) Camera->SetFieldOfView(OriginalFOV);
 if (auto* PC=Cast<APlayerController>(GetOwner()))
  if (ActiveShake && PC->PlayerCameraManager) PC->PlayerCameraManager->StopCameraShake(ActiveShake,true);
 ActiveShake=nullptr; bOwnTimeScale=false; bOwnCamera=false;
}
void UCapturePresentationComponent::RestoreTarget(FCaptureVisualTarget& T)
{
 if (T.Mesh.IsValid() && T.Glow && T.Mesh->GetOverlayMaterial()==T.Glow) T.Mesh->SetOverlayMaterial(T.OriginalOverlay);
 if (T.Mesh.IsValid()) T.Mesh->SetVisibility(T.bOriginalVisible, false);
 if (IsValid(T.DataMesh)) T.DataMesh->DestroyComponent();
 for (FCaptureFragment& F : T.Fragments) if (IsValid(F.Mesh)) F.Mesh->DestroyComponent();
 if (IsValid(T.VFX)) T.VFX->DestroyComponent();
 if (T.Boar.IsValid()) T.Boar->OnDestroyed.RemoveDynamic(this,&ThisClass::OnTargetDestroyed);
}
void UCapturePresentationComponent::CancelPresentation()
{
 RestoreSharedState(); bSharedActive=false;
 for (auto& T:Targets) RestoreTarget(T);
 Targets.Reset();
 for (auto& R:Receptions) if (IsValid(R.VFX)) R.VFX->DestroyComponent();
 Receptions.Reset();
}
void UCapturePresentationComponent::OnTargetDestroyed(AActor* Actor)
{
 for (int32 I=Targets.Num()-1;I>=0;--I) if (Targets[I].Boar.Get()==Actor || !Targets[I].Boar.IsValid())
 { RestoreTarget(Targets[I]); Targets.RemoveAt(I); }
 if (Targets.IsEmpty()) { RestoreSharedState(); bSharedActive=false; }
}
void UCapturePresentationComponent::StartDataConversion(FCaptureVisualTarget& T)
{
 T.bDataStarted = true;
 ABoarBase* Boar = T.Boar.Get();
 USkeletalMeshComponent* Source = T.Mesh.Get();
 if (!Boar || !Source) return;
 if (DataMaterial && Source->GetSkeletalMeshAsset())
 {
  USkeletalMeshComponent* Proxy = NewObject<USkeletalMeshComponent>(Boar, NAME_None, RF_Transient);
  Proxy->SetMobility(EComponentMobility::Movable);
  Proxy->SetupAttachment(Source->GetAttachParent() ? Source->GetAttachParent() : Boar->GetRootComponent());
  Proxy->SetRelativeTransform(Source->GetRelativeTransform());
  Proxy->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
  Proxy->SetLeaderPoseComponent(Source);
  Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Proxy->SetGenerateOverlapEvents(false);
  Proxy->SetCanEverAffectNavigation(false);
  Proxy->SetCastShadow(false);
  T.DataMaterial = UMaterialInstanceDynamic::Create(DataMaterial, this);
  T.DataMaterial->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(.01f,.72f,1.f));
  T.DataMaterial->SetScalarParameterValue(TEXT("CaptureMinZ"),T.CaptureMinZ);
  T.DataMaterial->SetScalarParameterValue(TEXT("CaptureHeight"),T.CaptureHeight);
  T.DataMaterial->SetScalarParameterValue(TEXT("DissolveProgress"),0.f);
  for (int32 Slot=0;Slot<FMath::Max(1,Proxy->GetNumMaterials());++Slot) Proxy->SetMaterial(Slot,T.DataMaterial);
  Proxy->RegisterComponent();
  T.DataMesh = Proxy;
  Source->SetVisibility(false,false);
 }
 // Small cubes and narrow vertical slivers travel upward with the data silhouette.
 UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 if (!Cube || !GlowMaterial) return;
 T.FragmentMaterial = UMaterialInstanceDynamic::Create(GlowMaterial,this);
 T.FragmentMaterial->SetVectorParameterValue(TEXT("GlowColor"),FLinearColor(.01f,.75f,1.f));
 T.FragmentMaterial->SetScalarParameterValue(TEXT("GlowStrength"),8.f);
 FRandomStream Random(Boar->GetUniqueID());
 for (int32 I=0;I<FMath::Clamp(FragmentCount,0,32);++I)
 {
  UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Boar,NAME_None,RF_Transient);
  Piece->SetMobility(EComponentMobility::Movable);
  Piece->SetStaticMesh(Cube);
  Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Piece->SetGenerateOverlapEvents(false);
  Piece->SetCanEverAffectNavigation(false);
  Piece->SetCastShadow(false);
  Piece->SetMaterial(0,T.FragmentMaterial);
  Piece->RegisterComponent();
  const bool bStreak = I % 4 == 0;
  Piece->SetWorldScale3D(bStreak ? FVector(.012f,.012f,Random.FRandRange(.16f,.3f)) : FVector(Random.FRandRange(.025f,.065f)));
  FCaptureFragment F; F.Mesh=Piece;
  F.Start=T.CaptureLocation + FVector(Random.FRandRange(-65.f,65.f),Random.FRandRange(-65.f,65.f),Random.FRandRange(-25.f,65.f));
  F.Drift=FVector(Random.FRandRange(-35.f,35.f),Random.FRandRange(-35.f,35.f),Random.FRandRange(120.f,260.f));
  F.Delay=Random.FRandRange(0.f,.17f);
  Piece->SetWorldLocation(F.Start);
  T.Fragments.Add(F);
 }
}
void UCapturePresentationComponent::StartCageReception(const FVector& Location)
{
 UNiagaraSystem* System = CageReceptionVFX ? CageReceptionVFX.Get() : CaptureVFX.Get();
 if (!System) return;
 FCaptureReception R;
 R.VFX=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Location-FVector(0,0,35),FRotator::ZeroRotator,FVector(VFXScale*.65f),false,false);
 if (R.VFX)
 {
  R.VFX->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(.02f,.9f,1.f,.9f));
  R.VFX->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
  R.VFX->SetCanRenderWhileSeeking(true);
  R.VFX->Activate(true);
  Receptions.Add(R);
 }
}
void UCapturePresentationComponent::OnWorldTick(UWorld* World,ELevelTick TickType,float DeltaSeconds)
{
 if (World!=GetWorld()) return;
 const double Now=World->GetRealTimeSeconds();
 const float RealDelta=FMath::Max(0.f,float(Now-LastRealTime)); LastRealTime=Now;
 if (Targets.IsEmpty() && Receptions.IsEmpty()) return;
 auto* Mode=World->GetAuthGameMode<ABoarGameMode>();
 if (Mode && Mode->GetStageState()!=EBoarStageState::Playing) { CancelPresentation(); return; }
 // Pause suspends the sequence, but never leaves the world or camera in a slowed state.
 if (UGameplayStatics::IsGamePaused(this)) { RestoreSharedState(); return; }
 auto* PC=Cast<APlayerController>(GetOwner());
 if (!PC || !PC->GetPawn()) { CancelPresentation(); return; }
 if (bSharedActive)
 {
  SharedAge+=RealDelta;
  if (SharedAge<HitStopDuration+SlowDuration)
  {
   bOwnTimeScale=true;
   UGameplayStatics::SetGlobalTimeDilation(this,OriginalTimeScale*FMath::Clamp(SharedAge<HitStopDuration?HitStopTimeScale:SlowTimeScale,.001f,1.f));
  }
  else if (bOwnTimeScale) { UGameplayStatics::SetGlobalTimeDilation(this,OriginalTimeScale); bOwnTimeScale=false; }
  if (Camera.IsValid())
  {
   bOwnCamera=true;
   float P=FMath::Clamp(SharedAge/FMath::Max(.01f,CameraDuration),0.f,1.f);
   Camera->SetFieldOfView(FMath::Clamp(OriginalFOV-CameraZoomAmount*FMath::Sin(P*PI),5.f,170.f));
  }
  const float DataStart=HitStopDuration+SlowDuration;
  if (!bTransferSoundPlayed && SharedAge>=DataStart) { PlaySound(TransferStartSound); bTransferSoundPlayed=true; }
  if (!bConfirmed && SharedAge>=GetTotalDuration()) { PlaySound(CaptureSuccessSound); bConfirmed=true; }
  if (SharedAge>=FMath::Max(GetTotalDuration(),CameraDuration)) { RestoreSharedState(); bSharedActive=false; }
 }
 TArray<TWeakObjectPtr<ABoarBase>> Finished;
 for (int32 I=Targets.Num()-1;I>=0;--I)
 {
  auto& T=Targets[I];T.Age+=RealDelta;
  const float DataStart=HitStopDuration+SlowDuration;
  const float P=FMath::Clamp(T.Age/FMath::Max(.01f,DataStart),0.f,1.f);
  if (T.Glow)
  {
   const float Envelope=FMath::Lerp(.15f,1.f,FMath::SmoothStep(0.f,1.f,P));
   T.Glow->SetScalarParameterValue(TEXT("GlowAmount"),Envelope);
   T.Glow->SetVectorParameterValue(TEXT("GlowColor"),FMath::Lerp(FLinearColor(.01f,.52f,.9f),FLinearColor(.04f,.88f,1.f),P));
  }
  if (IsValid(T.VFX))
  {
   T.VFX->SetDesiredAge(T.Age);
   const float RingEnvelope=FMath::Clamp((GetTotalDuration()-T.Age)/FMath::Max(.01f,TransferFinishDuration),0.f,1.f);
   T.VFX->SetWorldScale3D(FVector(VFXScale*FMath::Lerp(.45f,1.25f,FMath::Clamp(T.Age/.22f,0.f,1.f))));
   T.VFX->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(.01f,.75f,1.f,.8f*RingEnvelope));
  }
  if (T.Age>=DataStart && !T.bDataStarted) StartDataConversion(T);
  if (T.bDataStarted)
  {
   const float DataAge=FMath::Max(0.f,T.Age-DataStart);
   const float DissolveP=FMath::Clamp(DataAge/FMath::Max(.01f,CaptureDissolveDuration),0.f,1.f);
   const float FinishP=FMath::Clamp((DataAge-CaptureDissolveDuration)/FMath::Max(.01f,TransferFinishDuration),0.f,1.f);
   if (T.DataMaterial)
   {
    T.DataMaterial->SetScalarParameterValue(TEXT("DissolveProgress"),FMath::Lerp(0.f,.84f,DissolveP)+.17f*FinishP);
    T.DataMaterial->SetScalarParameterValue(TEXT("GlowAmount"),1.f+.45f*(1.f-DissolveP));
   }
   if (IsValid(T.DataMesh) && T.Mesh.IsValid())
    T.DataMesh->SetRelativeLocation(T.Mesh->GetRelativeLocation()+FVector(0,0,TransferRiseDistance*FinishP));
   for (FCaptureFragment& F:T.Fragments) if (IsValid(F.Mesh))
   {
    const float FragmentAge=FMath::Max(0.f,DataAge-F.Delay);
    F.Mesh->SetVisibility(DataAge>=F.Delay && FinishP<1.f);
    F.Mesh->SetWorldLocation(F.Start+F.Drift*FragmentAge+FVector(0,0,TransferRiseDistance*FinishP));
   }
   if (T.FragmentMaterial) T.FragmentMaterial->SetScalarParameterValue(TEXT("GlowAmount"),.9f*(1.f-FinishP));
  }
  if (!T.Boar.IsValid() || !T.Boar->IsCaptured() || T.Age>=GetTotalDuration())
  { Finished.Insert(T.Boar,0);RestoreTarget(T);Targets.RemoveAt(I); }
 }
 // Restore globals before invoking gameplay: this call can trigger the existing clear sequence.
 if (Targets.IsEmpty()) { RestoreSharedState();bSharedActive=false; }
 bCommittingCaptures = true;
 for (auto Boar:Finished) if (Mode && Boar.IsValid() && Boar->IsCaptured())
 {
  const FVector CaptureLocation=Boar->GetActorLocation();
  Mode->HandleBoarCaptured(Boar.Get());
  if (Boar.IsValid() && Boar->IsCaptured() && !Boar->GetActorLocation().Equals(CaptureLocation,1.f))
   StartCageReception(Boar->GetActorLocation());
 }
 bCommittingCaptures = false;
 if (Mode && !Finished.IsEmpty()) Mode->RefreshHousedBoarCount();
 const bool bHadReceptions=!Receptions.IsEmpty();
 for (int32 I=Receptions.Num()-1;I>=0;--I)
 {
  FCaptureReception& R=Receptions[I]; R.Age+=RealDelta;
  if (IsValid(R.VFX))
  {
   R.VFX->SetDesiredAge(R.Age);
   const float Fade=1.f-FMath::Clamp(R.Age/FMath::Max(.01f,CageReceptionDuration),0.f,1.f);
   R.VFX->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(.02f,.9f,1.f,.9f*Fade));
   R.VFX->SetWorldScale3D(FVector(VFXScale*FMath::Lerp(.3f,.85f,1.f-Fade)));
  }
  if (R.Age>=CageReceptionDuration || !IsValid(R.VFX))
  {
   if (IsValid(R.VFX)) R.VFX->DestroyComponent();
   Receptions.RemoveAt(I);
  }
 }
 if (bHadReceptions && Receptions.IsEmpty() && Targets.IsEmpty() && Mode) Mode->RefreshHousedBoarCount();
}
