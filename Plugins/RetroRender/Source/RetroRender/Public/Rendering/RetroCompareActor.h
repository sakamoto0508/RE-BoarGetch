#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rendering/RetroRenderTypes.h"
#include "RetroCompareActor.generated.h"
class URetroRenderComponent;
class URetroDebugWidget;
class URetroStatusWidget;
class APlayerController;

// Opt-in test harness. No changes to existing GameMode, controller, camera or maps.
UCLASS(NotBlueprintable)
class RETRORENDER_API ARetroCompareActor : public AActor
{
 GENERATED_BODY()
public:
 ARetroCompareActor();
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<URetroRenderComponent> Renderer;
 UPROPERTY(EditAnywhere, Category="Retro|Debug") bool bEnableDebugUI = true;
 UFUNCTION(BlueprintCallable) void ToggleDebugUI();
 UFUNCTION(BlueprintCallable) void CaptureCurrent();
 UFUNCTION(BlueprintCallable) void CaptureAll();
 UFUNCTION(BlueprintPure) bool IsCapturing() const { return CaptureIndex >= 0; }
 // Saves the next rendered frame as Saved/Screenshots/Retro/<Label>.png without touching camera/pause (automation).
 UFUNCTION(BlueprintCallable) bool RequestLabeledScreenshot(const FString& Label);
 UFUNCTION(BlueprintPure) bool IsScreenshotPending() const;
 // Total GPU frame time, not an isolated measurement of this post-process pass.
 UFUNCTION(BlueprintPure) float GetGPUFrameMilliseconds() const;
 UFUNCTION(BlueprintCallable) void SetDebugViewportSize(int32 Width, int32 Height);
 UFUNCTION(BlueprintPure) FIntPoint GetDebugViewportSize() const;
 virtual void Tick(float DeltaSeconds) override;
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void CloseDebugUI();
 void ScreenshotProcessed();
 void FinishCapture();
 void RequestImage();
 UPROPERTY(Transient) TObjectPtr<URetroDebugWidget> DebugUI;
 UPROPERTY(Transient) TObjectPtr<URetroStatusWidget> StatusUI;
 UPROPERTY(Transient) TObjectPtr<APlayerController> Player;
 UPROPERTY(Transient) TObjectPtr<AActor> FixedCamera;
 UPROPERTY(Transient) TObjectPtr<AActor> PreviousViewTarget;
 ERetroRenderPreset PreviousPreset = ERetroRenderPreset::OFF;
 int32 CaptureIndex=-1;
 int32 WarmupFrames=0;
 bool bAwaitingScreenshot=false;
 bool bWasPaused=false;
 bool bWasUIOpen=false;
 bool bPreviousCursor=false;
 FString CaptureStamp;
 double CaptureDeadline=0;
 double NextStatusTime=0;
 bool bOwnViewportSize=false;
 FDelegateHandle ScreenshotHandle;
};
