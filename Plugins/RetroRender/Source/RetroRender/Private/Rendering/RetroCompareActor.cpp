#include "Rendering/RetroCompareActor.h"
#include "Rendering/RetroRenderComponent.h"
#include "Rendering/RetroDebugWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "DynamicRHI.h"
#include "Slate/SceneViewport.h"

ARetroCompareActor::ARetroCompareActor()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.bTickEvenWhenPaused=true;
 InputPriority=100;
 Renderer=CreateDefaultSubobject<URetroRenderComponent>(TEXT("RetroRenderer"));
}
float ARetroCompareActor::GetGPUFrameMilliseconds() const
{
 return FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
}
void ARetroCompareActor::SetDebugViewportSize(int32 Width,int32 Height)
{
#if !UE_BUILD_SHIPPING
 if (auto* Viewport=GetWorld()->GetGameViewport())
  if (auto* SceneViewport=Viewport->GetGameViewport())
  {
   bOwnViewportSize=Width>0 && Height>0;
   SceneViewport->SetFixedViewportSize(bOwnViewportSize?FMath::Clamp(Width,64,4096):0,bOwnViewportSize?FMath::Clamp(Height,64,4096):0);
  }
#endif
}
FIntPoint ARetroCompareActor::GetDebugViewportSize() const
{
 auto* Viewport=GetWorld()->GetGameViewport();
 return Viewport && Viewport->Viewport ? Viewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
}
void ARetroCompareActor::BeginPlay()
{
 Super::BeginPlay();
#if !UE_BUILD_SHIPPING
 Player=UGameplayStatics::GetPlayerController(this,0);
 if (!bEnableDebugUI || !Player || !Player->IsLocalController()) return;
 EnableInput(Player);
 auto& Binding=InputComponent->BindKey(FInputChord(EKeys::F7,false,true,true,false),IE_Pressed,this,&ThisClass::ToggleDebugUI);
 Binding.bExecuteWhenPaused=true;
 StatusUI=CreateWidget<URetroStatusWidget>(Player);
 StatusUI->AddToViewport(5); StatusUI->SetVisibility(ESlateVisibility::HitTestInvisible);
 StatusUI->Refresh(Renderer);
#endif
}
void ARetroCompareActor::ToggleDebugUI()
{
#if !UE_BUILD_SHIPPING
 if (!Player || !bEnableDebugUI || IsCapturing()) return;
 if (DebugUI && DebugUI->IsInViewport()) { CloseDebugUI(); return; }
 // Existing menu owns its input mode. Only open from gameplay, preserving that ownership.
 if (Player->bShowMouseCursor || UGameplayStatics::IsGamePaused(this)) return;
 bPreviousCursor=Player->bShowMouseCursor;
 if (!DebugUI) { DebugUI=CreateWidget<URetroDebugWidget>(Player); DebugUI->Configure(this); }
 DebugUI->AddToViewport(10000);
 if (StatusUI) StatusUI->SetVisibility(ESlateVisibility::Collapsed);
 Player->SetIgnoreMoveInput(true); Player->SetIgnoreLookInput(true);
 Player->SetShowMouseCursor(true);
 FInputModeUIOnly Mode; Mode.SetWidgetToFocus(DebugUI->TakeWidget());
 Player->SetInputMode(Mode);
#endif
}
void ARetroCompareActor::CloseDebugUI()
{
 if (!DebugUI || !DebugUI->IsInViewport()) return;
 DebugUI->RemoveFromParent();
 if (StatusUI) StatusUI->SetVisibility(ESlateVisibility::HitTestInvisible);
 if (Player)
 {
  Player->SetIgnoreMoveInput(false); Player->SetIgnoreLookInput(false);
  Player->SetShowMouseCursor(bPreviousCursor); Player->SetInputMode(FInputModeGameOnly());
 }
}
void ARetroCompareActor::CaptureCurrent()
{
#if !UE_BUILD_SHIPPING
 if (IsCapturing() || FScreenshotRequest::IsScreenshotRequested()) return;
 Player=UGameplayStatics::GetPlayerController(this,0);
 if (!Player || !Player->PlayerCameraManager || !Renderer->IsReady()) return;
 bWasUIOpen=DebugUI && DebugUI->IsInViewport(); CloseDebugUI();
 PreviousPreset=Renderer->GetPreset(); CaptureIndex=5;
 bWasPaused=UGameplayStatics::IsGamePaused(this);
 UGameplayStatics::SetGamePaused(this,true);
 PreviousViewTarget=Player->GetViewTarget();
 auto* Camera=GetWorld()->SpawnActor<ACameraActor>();
 Camera->SetActorLocationAndRotation(Player->PlayerCameraManager->GetCameraLocation(),Player->PlayerCameraManager->GetCameraRotation());
 Camera->GetCameraComponent()->SetFieldOfView(Player->PlayerCameraManager->GetFOVAngle());
 const auto& POV=Player->PlayerCameraManager->GetCameraCacheView();
 Camera->GetCameraComponent()->SetAspectRatio(POV.AspectRatio);
 Camera->GetCameraComponent()->bConstrainAspectRatio=POV.bConstrainAspectRatio;
 Camera->GetCameraComponent()->PostProcessSettings=POV.PostProcessSettings;
 Camera->GetCameraComponent()->PostProcessBlendWeight=POV.PostProcessBlendWeight;
 FixedCamera=Camera; Player->SetViewTarget(Camera);
 CaptureStamp=FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
 ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this,&ThisClass::ScreenshotProcessed);
 WarmupFrames=30; CaptureDeadline=FPlatformTime::Seconds()+120;
#endif
}
bool ARetroCompareActor::RequestLabeledScreenshot(const FString& Label)
{
#if !UE_BUILD_SHIPPING
 if (IsCapturing() || FScreenshotRequest::IsScreenshotRequested()) return false;
 const FString Directory=FPaths::ProjectSavedDir()/TEXT("Screenshots/Retro");
 IFileManager::Get().MakeDirectory(*Directory,true);
 FScreenshotRequest::RequestScreenshot(Directory/(FPaths::MakeValidFileName(Label)+TEXT(".png")),false,false);
 return true;
#else
 return false;
#endif
}
bool ARetroCompareActor::IsScreenshotPending() const { return FScreenshotRequest::IsScreenshotRequested(); }
void ARetroCompareActor::CaptureAll()
{
 CaptureCurrent();
 if (!IsCapturing()) return;
 CaptureIndex=0; Renderer->SetPreset(ERetroRenderPreset::OFF);
}
void ARetroCompareActor::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
 if (StatusUI && !IsCapturing() && FPlatformTime::Seconds()>=NextStatusTime)
 {
  NextStatusTime=FPlatformTime::Seconds()+.2;
  StatusUI->Refresh(Renderer);
 }
#endif
 if (!IsCapturing()) return;
 if (FPlatformTime::Seconds()>CaptureDeadline) { UE_LOG(LogTemp,Error,TEXT("[Retro] Screenshot timeout")); FinishCapture(); return; }
 if (!bAwaitingScreenshot && --WarmupFrames<=0) RequestImage();
}
void ARetroCompareActor::RequestImage()
{
 if (FScreenshotRequest::IsScreenshotRequested()) return;
 const FString Directory=FPaths::ProjectSavedDir()/TEXT("Screenshots/Retro");
 IFileManager::Get().MakeDirectory(*Directory,true);
 const FString Name=Directory/FString::Printf(TEXT("%s_%s.png"),*CaptureStamp,*RetroPresetName(Renderer->GetPreset()));
 bAwaitingScreenshot=true;
 FScreenshotRequest::RequestScreenshot(Name,false,false);
 UE_LOG(LogTemp,Log,TEXT("[Retro] Screenshot requested: %s"),*Name);
}
void ARetroCompareActor::ScreenshotProcessed()
{
 if (!IsCapturing() || !bAwaitingScreenshot) return;
 bAwaitingScreenshot=false;
 if (CaptureIndex==5 || ++CaptureIndex>=5) { FinishCapture(); return; }
 Renderer->SetPreset(ERetroRenderPreset(CaptureIndex)); WarmupFrames=30;
}
void ARetroCompareActor::FinishCapture()
{
 FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
 if (Player && PreviousViewTarget) Player->SetViewTarget(PreviousViewTarget);
 if (FixedCamera) FixedCamera->Destroy(); FixedCamera=nullptr; PreviousViewTarget=nullptr;
 Renderer->SetPreset(PreviousPreset);
 UGameplayStatics::SetGamePaused(this,bWasPaused);
 CaptureIndex=-1; bAwaitingScreenshot=false;
 if (bWasUIOpen) ToggleDebugUI(); bWasUIOpen=false;
}
void ARetroCompareActor::EndPlay(const EEndPlayReason::Type Reason)
{
 bWasUIOpen=false;
 if (bOwnViewportSize) SetDebugViewportSize(0,0);
 if (IsCapturing()) FinishCapture();
 CloseDebugUI(); DisableInput(Player); Player=nullptr;
 if (StatusUI) StatusUI->RemoveFromParent(); StatusUI=nullptr;
 Super::EndPlay(Reason);
}
