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
	 *  commandlets) or when the read fails. One synchronous GPU read back: a one-frame hitch. */
	static UTexture2D* CaptureViewport(UGameViewportClient* ViewportClient, int32 MaxWidth);

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
	TObjectPtr<UMaterialInstanceDynamic> MaterialInstance = nullptr;
	TObjectPtr<UMaterialInterface> BaseMaterial = nullptr;
	FVector2D FallbackSize = FVector2D(1920.0, 1080.0);
};
