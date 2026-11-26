#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionTypes.h"
#include "UObject/GCObject.h"

class SACFTransitionOverlay;
class SWidget;
class UGameViewportClient;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;
class UTexture2D;

/**
 * Draws one transition frame (README 33 章「描画」): a Slate widget (SACFTransitionOverlay) with a dynamic instance of
 * M_ACFTransition and an optional text. It knows nothing about requests or worlds: UACFTransitionSubsystem puts it in the
 * game viewport, the Transition Preview tab and the tests put it in their own widgets / render targets.
 * Release() drops the material instance, the capture and the widget (nothing stays after a transition).
 */
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionView : public FGCObject
{
public:
	FACFTransitionView();
	virtual ~FACFTransitionView() override;

	/** The widget (created on first use). */
	TSharedRef<SWidget> GetWidget();

	/** Sets the frame: the effect at Coverage (0 = scene visible, 1 = covered), Time seconds since the start. */
	void Apply(const FACFTransitionSettings& Settings, EACFTransitionPhase Phase, float Coverage, float TimeSeconds, UTexture* Snapshot, bool bBlockInput = false);

	/** Size used for the aspect ratio when the widget has not been laid out yet (render targets). */
	void SetFallbackSize(const FVector2D& Size) { FallbackSize = Size; }

	UMaterialInstanceDynamic* GetMaterialInstance() const { return MaterialInstance; }

	void Release();

	/** The project's transition material (M_ACFTransition unless overridden), loaded synchronously. */
	static UMaterialInterface* LoadDefaultMaterial();

	/** Reads the game viewport (scene and UI) back into a transient texture. nullptr without a renderer (NullRHI,
	 *  commandlets) or when the read fails. Synchronous: Slate draws the window once more, the GPU result is read back and
	 *  the game thread waits for the render thread (a one-frame hitch, see README 33 章「キャプチャ」). The fallback of
	 *  RequestGpuCapture and the capture of ShowStill (tools, tests). */
	static UTexture2D* CaptureViewport(UGameViewportClient* ViewportClient, int32 MaxWidth);

	/** The game viewport's rectangle in its window's back buffer (pixels); false without a renderer / window. */
	static bool GetViewportPixelRect(UGameViewportClient* ViewportClient, FIntRect& OutRect);

	/**
	 * Starts a capture of the game viewport without stalling the game thread: when the window's next frame is ready to be
	 * presented, the render thread copies the viewport's part of the back buffer into a render target on the GPU (no read
	 * back to the CPU, no extra Slate draw). Ready one or two frames later; the caller waits for it before drawing the
	 * effect. nullptr when it cannot start (no renderer, no window); the request fails (GetState) when the back buffer
	 * format is not 8-bit BGRA (e.g. HDR output) or the window changed; the caller then uses CaptureViewport.
	 */
	static TSharedPtr<class FACFTransitionGpuCapture> RequestGpuCapture(UGameViewportClient* ViewportClient);

	/** Timing of the last capture (README 33 章「性能」, the Profile tests). */
	struct FCaptureTiming
	{
		bool bGpuCopy = false;
		double GameThreadMs = 0.0;  // time spent in the call on the game thread
		double ScreenshotMs = 0.0;  // CPU path: Slate redraw + GPU read back + render thread flush
		double ResizeMs = 0.0;      // CPU path: halving to MaxCaptureWidth
		double UploadMs = 0.0;      // CPU path: transient texture + copy + UpdateResource
		int32 Width = 0;
		int32 Height = 0;
	};
	static const FCaptureTiming& GetLastCaptureTiming();

	/** A synthetic game frame (sky, ground, coloured blocks, sun): the background and capture of the Transition Preview and the
	 *  render tests. */
	static UTexture2D* CreatePreviewPicture(int32 Width, int32 Height);

	/** Live objects created by views (the tests check that nothing leaks). */
	static int32 GetLiveViewCount();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FACFTransitionView"); }

private:
	void EnsureMaterial(const FACFTransitionSettings& Settings);

	TSharedPtr<SACFTransitionOverlay> Widget;
	float BackgroundBlurStrength = 0.f;
	TObjectPtr<UMaterialInstanceDynamic> MaterialInstance = nullptr;
	TObjectPtr<UMaterialInterface> BaseMaterial = nullptr;
	FVector2D FallbackSize = FVector2D(1920.0, 1080.0);
};

/** A pending GPU copy of the viewport (FACFTransitionView::RequestGpuCapture). Game thread object; cancels itself when destroyed. */
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionGpuCapture : public FGCObject
{
public:
	enum class EState : int32 { Pending, Ready, Failed };

	virtual ~FACFTransitionGpuCapture() override;
	EState GetState() const;
	/** The render target the frame is copied into (valid while pending; holds the frame once Ready). */
	UTexture* GetTexture() const;
	/** Frames since the request. */
	uint64 GetFramesWaited() const;
	/** Stops listening for the frame (the render target is left to the caller / GC). Safe to call any number of times. */
	void Cancel();
	/** The viewport still has the rectangle the copy was requested for (not resized / moved since): the copy is usable. */
	bool MatchesViewport(UGameViewportClient* ViewportClient) const;

	/** Live requests and registered render thread listeners (the tests check that nothing is left). */
	static int32 GetLiveCount();
	static int32 GetListenerCount();

	FACFTransitionGpuCapture();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FACFTransitionGpuCapture"); }

private:
	friend class FACFTransitionView;
	struct FShared;
	TSharedPtr<FShared, ESPMode::ThreadSafe> Shared;
	TObjectPtr<class UTextureRenderTarget2D> Target = nullptr;
	FDelegateHandle Handle;
	uint64 RequestFrame = 0;
	FIntRect RequestRect;
};
