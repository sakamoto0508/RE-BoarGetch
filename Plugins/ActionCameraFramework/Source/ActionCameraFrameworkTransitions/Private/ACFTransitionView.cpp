#include "ACFTransitionView.h"

#include "ACFTransitionCatalog.h"
#include "ACFTransitionSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/SlateViewportProvider.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "RHIGlobals.h"
#include "SlateMaterialBrush.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SViewport.h"

namespace ACFTransitionParams
{
	static const FName Effect(TEXT("Effect"));
	static const FName Progress(TEXT("Progress"));
	static const FName Opening(TEXT("Opening"));
	static const FName Intensity(TEXT("Intensity"));
	static const FName Direction(TEXT("Direction"));
	static const FName Seed(TEXT("Seed"));
	static const FName Aspect(TEXT("Aspect"));
	static const FName Time(TEXT("Time"));
	static const FName Softness(TEXT("Softness"));
	static const FName Count(TEXT("Count"));
	static const FName Reverse(TEXT("Reverse"));
	static const FName Origin(TEXT("Origin"));
	static const FName SnapshotEncoded(TEXT("SnapshotEncoded"));
	static const FName UseSnapshot(TEXT("UseSnapshot"));
	static const FName UseOverlay(TEXT("UseOverlay"));
	static const FName Color(TEXT("Color"));
	static const FName Color2(TEXT("Color2"));
	static const FName Center(TEXT("Center"));
	static const FName Snapshot(TEXT("Snapshot"));
	static const FName OverlayTex(TEXT("OverlayTexture"));
}

static int32 GACFLiveViews = 0;

/** The overlay: the material brush filling the widget and an optional text. Hit-test invisible unless input is locked. */
class SACFTransitionOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SACFTransitionOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&)
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(Image, SImage).Image(&Brush)
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 96.f, 64.f))
			[
				SAssignNew(CornerText, STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 30)).ShadowOffset(FVector2D(2.0, 2.0)).Visibility(EVisibility::Collapsed)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SAssignNew(CenterText, STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 64)).ShadowOffset(FVector2D(3.0, 3.0)).Visibility(EVisibility::Collapsed)
			]
		];
		SetVisibility(EVisibility::HitTestInvisible);
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		LastSize = AllottedGeometry.GetLocalSize();
	}

	void SetMaterial(UMaterialInterface* Material)
	{
		Brush.SetResourceObject(Material);
		Brush.ImageSize = FVector2D(32.0, 32.0);
	}

	void SetText(const FText& Text, float Opacity, bool bCenter)
	{
		const bool bShow = !Text.IsEmpty() && Opacity > 0.01f;
		TSharedPtr<STextBlock> Shown = bCenter ? CenterText : CornerText;
		TSharedPtr<STextBlock> Hidden = bCenter ? CornerText : CenterText;
		Hidden->SetVisibility(EVisibility::Collapsed);
		Shown->SetVisibility(bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShow)
		{
			if (!Shown->GetText().EqualTo(Text))
			{
				Shown->SetText(Text);
			}
			Shown->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, Opacity)));
		}
	}

	void SetBlockInput(bool bBlock)
	{
		const EVisibility Wanted = bBlock ? EVisibility::Visible : EVisibility::HitTestInvisible;
		if (GetVisibility() != Wanted)
		{
			SetVisibility(Wanted);
		}
	}

	// Absorb clicks while input is locked (Visible).
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }

	FVector2D LastSize = FVector2D::ZeroVector;

private:
	FSlateBrush Brush;
	TSharedPtr<SImage> Image;
	TSharedPtr<STextBlock> CornerText;
	TSharedPtr<STextBlock> CenterText;
};

FACFTransitionView::FACFTransitionView()
{
	++GACFLiveViews;
}

FACFTransitionView::~FACFTransitionView()
{
	Release();
	--GACFLiveViews;
}

int32 FACFTransitionView::GetLiveViewCount()
{
	return GACFLiveViews;
}

TSharedRef<SWidget> FACFTransitionView::GetWidget()
{
	if (!Widget.IsValid())
	{
		Widget = SNew(SACFTransitionOverlay);
		if (MaterialInstance)
		{
			Widget->SetMaterial(MaterialInstance);
		}
	}
	return Widget.ToSharedRef();
}

UMaterialInterface* FACFTransitionView::LoadDefaultMaterial()
{
	const UACFTransitionProjectSettings* Settings = GetDefault<UACFTransitionProjectSettings>();
	UMaterialInterface* Material = Cast<UMaterialInterface>(Settings->TransitionMaterial.TryLoad());
	if (!Material)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogACFTransition, Error, TEXT("ACF Transition: material %s not found. Transitions keep their timing and events but draw a plain colour."), *Settings->TransitionMaterial.ToString());
		}
	}
	return Material;
}

void FACFTransitionView::EnsureMaterial(const FACFTransitionSettings& Settings)
{
	UMaterialInterface* Wanted = Settings.MaterialOverride ? Settings.MaterialOverride.Get() : LoadDefaultMaterial();
	if (!Wanted)
	{
		return;
	}
	if (!MaterialInstance || BaseMaterial != Wanted)
	{
		BaseMaterial = Wanted;
		MaterialInstance = UMaterialInstanceDynamic::Create(Wanted, GetTransientPackage());
		if (Widget.IsValid())
		{
			Widget->SetMaterial(MaterialInstance);
		}
	}
}

void FACFTransitionView::Apply(const FACFTransitionSettings& Settings, EACFTransitionPhase Phase, float Coverage, float TimeSeconds, UTexture* Snapshot, bool bBlockInput)
{
	using namespace ACFTransitionParams;
	EnsureMaterial(Settings);
	FVector2D Size = Widget.IsValid() ? Widget->LastSize : FVector2D::ZeroVector;
	if (Size.X < 1.0 || Size.Y < 1.0)
	{
		Size = FallbackSize;
	}
	if (UMaterialInstanceDynamic* MID = MaterialInstance)
	{
		MID->SetScalarParameterValue(Effect, static_cast<float>(Settings.Effect));
		MID->SetScalarParameterValue(Progress, FMath::Clamp(Coverage, 0.f, 1.f));
		MID->SetScalarParameterValue(Opening, Phase == EACFTransitionPhase::Open ? 1.f : 0.f);
		MID->SetScalarParameterValue(Intensity, Settings.Intensity);
		MID->SetScalarParameterValue(Direction, Settings.DirectionDegrees);
		MID->SetScalarParameterValue(Seed, Settings.Seed);
		MID->SetScalarParameterValue(Aspect, static_cast<float>(Size.X / FMath::Max(Size.Y, 1.0)));
		MID->SetScalarParameterValue(Time, TimeSeconds);
		MID->SetScalarParameterValue(Softness, Settings.Softness);
		MID->SetScalarParameterValue(Count, FMath::Max(Settings.Count, 0.25f));
		MID->SetScalarParameterValue(Reverse, Settings.bReverse ? -1.f : 1.f);
		MID->SetScalarParameterValue(Origin, Settings.bFromCenter ? 1.f : 0.f);
		MID->SetScalarParameterValue(UseSnapshot, Snapshot ? 1.f : 0.f);
		// The 10-bit GPU copy is not an sRGB texture: the material decodes it.
		const UTextureRenderTarget2D* SnapshotTarget = Cast<UTextureRenderTarget2D>(Snapshot);
		MID->SetScalarParameterValue(SnapshotEncoded, SnapshotTarget && !SnapshotTarget->IsSRGB() ? 1.f : 0.f);
		MID->SetScalarParameterValue(UseOverlay, Settings.OverlayTexture ? 1.f : 0.f);
		MID->SetVectorParameterValue(Color, Settings.Color);
		MID->SetVectorParameterValue(Color2, Settings.SecondaryColor);
		MID->SetVectorParameterValue(Center, FLinearColor(Settings.Center.X, Settings.Center.Y, 0.f, 0.f));
		if (Snapshot)
		{
			MID->SetTextureParameterValue(ACFTransitionParams::Snapshot, Snapshot);
		}
		if (Settings.OverlayTexture)
		{
			MID->SetTextureParameterValue(OverlayTex, Settings.OverlayTexture);
		}
	}
	if (Widget.IsValid())
	{
		const bool bCard = Settings.Effect == EACFTransitionEffect::StageCard;
		// The text appears in the last fifth of the coverage (readable only once the screen is covered).
		const float TextOpacity = FMath::Clamp((Coverage - 0.8f) / 0.2f, 0.f, 1.f);
		Widget->SetText(Settings.LoadingText, TextOpacity, bCard);
		Widget->SetBlockInput(bBlockInput);
	}
}

void FACFTransitionView::Release()
{
	if (Widget.IsValid())
	{
		Widget->SetMaterial(nullptr);
	}
	Widget.Reset();
	MaterialInstance = nullptr;
	BaseMaterial = nullptr;
}

void FACFTransitionView::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MaterialInstance);
	Collector.AddReferencedObject(BaseMaterial);
}

namespace ACFTransitionCapture
{
	static FACFTransitionView::FCaptureTiming GLastTiming;
	static int32 GLiveGpuCaptures = 0; // game thread
	static int32 GListeners = 0;       // game thread

#if !UE_BUILD_SHIPPING
	static TAutoConsoleVariable<int32> CVarFailCapture(TEXT("ACF.Transition.Debug.FailCapture"), 0,
		TEXT("Tests: 1 = every screen capture fails (GPU copy and read back), to check that the transitions recover (procedural look)."));
	static bool ForceFail() { return CVarFailCapture.GetValueOnGameThread() != 0; }
#else
	static bool ForceFail() { return false; }
#endif

	/** The viewport widget's rectangle in its window's back buffer (pixels), as FSlateApplication::TakeScreenshot computes it. */
	static bool GetViewportRect(UGameViewportClient* ViewportClient, TSharedPtr<SWindow>& OutWindow, FIntRect& OutRect)
	{
		if (!ViewportClient || GUsingNullRHI || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
		{
			return false;
		}
		TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
		if (!ViewportWidget.IsValid())
		{
			return false;
		}
		OutWindow = FSlateApplication::Get().FindWidgetWindow(ViewportWidget.ToSharedRef());
		if (!OutWindow.IsValid())
		{
			return false;
		}
		const FGeometry& Geometry = ViewportWidget->GetTickSpaceGeometry();
		const FVector2f Position = FVector2f(Geometry.GetAbsolutePosition()) - FVector2f(OutWindow->GetPositionInScreen());
		const FVector2f Size = FVector2f(Geometry.GetDrawSize());
		const int32 X = FMath::RoundToInt(Position.X);
		const int32 Y = FMath::RoundToInt(Position.Y);
		OutRect = FIntRect(X, Y, X + FMath::RoundToInt(Size.X), Y + FMath::RoundToInt(Size.Y));
		return OutRect.Width() >= 4 && OutRect.Height() >= 4 && OutRect.Min.X >= 0 && OutRect.Min.Y >= 0;
	}
}

bool FACFTransitionView::GetViewportPixelRect(UGameViewportClient* ViewportClient, FIntRect& OutRect)
{
	TSharedPtr<SWindow> Window;
	return ACFTransitionCapture::GetViewportRect(ViewportClient, Window, OutRect);
}

const FACFTransitionView::FCaptureTiming& FACFTransitionView::GetLastCaptureTiming()
{
	return ACFTransitionCapture::GLastTiming;
}

UTexture2D* FACFTransitionView::CaptureViewport(UGameViewportClient* ViewportClient, int32 MaxWidth)
{
	using namespace ACFTransitionCapture;
	const double T0 = FPlatformTime::Seconds();
	GLastTiming = FCaptureTiming();
	if (!ViewportClient || GUsingNullRHI || !FApp::CanEverRender() || !FSlateApplication::IsInitialized() || ForceFail())
	{
		return nullptr;
	}
	TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
	if (!ViewportWidget.IsValid())
	{
		return nullptr;
	}
	TArray<FColor> Pixels;
	FIntVector Size;
	const bool bRead = FSlateApplication::Get().TakeScreenshot(ViewportWidget.ToSharedRef(), Pixels, Size);
	const double T1 = FPlatformTime::Seconds();
	GLastTiming.ScreenshotMs = (T1 - T0) * 1000.0;
	if (!bRead || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() != Size.X * Size.Y)
	{
		return nullptr;
	}
	int32 W = Size.X;
	int32 H = Size.Y;
	while (W > MaxWidth && W >= 4 && H >= 4)
	{
		const int32 NW = W / 2;
		const int32 NH = H / 2;
		TArray<FColor> Half;
		Half.SetNumUninitialized(NW * NH);
		for (int32 Y = 0; Y < NH; ++Y)
		{
			for (int32 X = 0; X < NW; ++X)
			{
				const FColor& A = Pixels[(2 * Y) * W + 2 * X];
				const FColor& B = Pixels[(2 * Y) * W + 2 * X + 1];
				const FColor& C = Pixels[(2 * Y + 1) * W + 2 * X];
				const FColor& D = Pixels[(2 * Y + 1) * W + 2 * X + 1];
				Half[Y * NW + X] = FColor((A.R + B.R + C.R + D.R) / 4, (A.G + B.G + C.G + D.G) / 4, (A.B + B.B + C.B + D.B) / 4, 255);
			}
		}
		Pixels = MoveTemp(Half);
		W = NW;
		H = NH;
	}
	const double T2 = FPlatformTime::Seconds();
	GLastTiming.ResizeMs = (T2 - T1) * 1000.0;
	UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("ACFTransitionCapture")));
	if (!Texture)
	{
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	const double T3 = FPlatformTime::Seconds();
	GLastTiming.UploadMs = (T3 - T2) * 1000.0;
	GLastTiming.GameThreadMs = (T3 - T0) * 1000.0;
	GLastTiming.Width = W;
	GLastTiming.Height = H;
	return Texture;
}

// ------------------------------------------------------------------------------------------------ GPU copy

struct FACFTransitionGpuCapture::FShared
{
	std::atomic<int32> State { static_cast<int32>(EState::Pending) };
	const SWindow* Window = nullptr;   // compared only, never dereferenced on the render thread
	FIntRect Rect;
	// Raw pointer, read on the render thread only inside the listener. The resource is released by a render command that
	// the game thread queues when the GC destroys Target (UTexture::BeginDestroy -> ReleaseResource, FinishDestroy waits
	// for it). Target is referenced by the FACFTransitionGpuCapture until the listener is removed, so that release command
	// is always queued after every Slate draw command that can still call the listener: the render thread runs commands
	// in order, so the listener never sees a released resource. The copy itself is recorded on the immediate command list
	// before that release, and the RHI keeps a texture alive until the commands that use it have run.
	FTextureRenderTargetResource* Resource = nullptr;
};

TSharedPtr<FACFTransitionGpuCapture> FACFTransitionView::RequestGpuCapture(UGameViewportClient* ViewportClient)
{
	using namespace ACFTransitionCapture;
	const double T0 = FPlatformTime::Seconds();
	TSharedPtr<SWindow> Window;
	FIntRect Rect;
	if (!GetViewportRect(ViewportClient, Window, Rect) || !FSlateApplication::Get().GetRenderer())
	{
		return nullptr;
	}
	// The copy keeps the back buffer's format: 8-bit BGRA (an sRGB texture, decoded by the sampler) or 10-bit A2B10G10R10
	// (the UE 5 default, r.DefaultBackBufferPixelFormat 4; decoded by the material). HDR output and float back buffers are
	// not sRGB-encoded: the read back is used.
	static const auto CVarBackBufferFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
	static const auto CVarHDROutput = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.EnableHDROutput"));
	const EPixelFormat Format = CVarBackBufferFormat ? EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarBackBufferFormat->GetValueOnGameThread())) : PF_B8G8R8A8;
	if ((Format != PF_B8G8R8A8 && Format != PF_A2B10G10R10) || (CVarHDROutput && CVarHDROutput->GetValueOnGameThread() != 0))
	{
		return nullptr;
	}
	TSharedPtr<FACFTransitionGpuCapture> Capture = MakeShared<FACFTransitionGpuCapture>();
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Target->ClearColor = FLinearColor::Black;
	Target->AddressX = TA_Clamp;
	Target->AddressY = TA_Clamp;
	// The bytes are copied as they are. 8-bit: an sRGB texture, like the read back texture of CaptureViewport (SRGB = true).
	Target->InitCustomFormat(Rect.Width(), Rect.Height(), Format, /*bForceLinearGamma*/ Format != PF_B8G8R8A8);
	Capture->Target = Target;
	Capture->RequestFrame = GFrameCounter;
	Capture->RequestRect = Rect;
	Capture->Shared = MakeShared<FACFTransitionGpuCapture::FShared, ESPMode::ThreadSafe>();
	Capture->Shared->Window = Window.Get();
	Capture->Shared->Rect = Rect;
	Capture->Shared->Resource = Target->GameThread_GetRenderTargetResource();
	if (ForceFail())
	{
		Capture->Shared->State = static_cast<int32>(FACFTransitionGpuCapture::EState::Failed);
	}
	else
	{
		TWeakPtr<FACFTransitionGpuCapture::FShared, ESPMode::ThreadSafe> Weak = Capture->Shared;
		++GListeners;
		Capture->Handle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddLambda([Weak](SWindow& InWindow, ISlateViewportProvider& Provider)
		{
			TSharedPtr<FACFTransitionGpuCapture::FShared, ESPMode::ThreadSafe> Shared = Weak.Pin();
			if (!Shared || &InWindow != Shared->Window || Shared->State.load() != static_cast<int32>(FACFTransitionGpuCapture::EState::Pending))
			{
				return;
			}
			FRHITexture* BackBuffer = Provider.GetBackBufferResource();
			FRHITexture* Dest = Shared->Resource ? Shared->Resource->GetRenderTargetTexture() : nullptr;
			const FIntRect& R = Shared->Rect;
			if (!Dest)
			{
				return; // the render target is created on the render thread: the next frame
			}
			if (!BackBuffer || BackBuffer->GetFormat() != Dest->GetFormat()
				|| R.Max.X > (int32)BackBuffer->GetSizeX() || R.Max.Y > (int32)BackBuffer->GetSizeY()
				|| (int32)Dest->GetSizeX() != R.Width() || (int32)Dest->GetSizeY() != R.Height())
			{
				UE_LOG(LogACFTransition, Log, TEXT("GPU capture not possible: back buffer %s %dx%d, target %s %dx%d, viewport rect (%d,%d)-(%d,%d)."),
					BackBuffer ? GPixelFormats[BackBuffer->GetFormat()].Name : TEXT("none"), BackBuffer ? (int32)BackBuffer->GetSizeX() : 0, BackBuffer ? (int32)BackBuffer->GetSizeY() : 0,
					Dest ? GPixelFormats[Dest->GetFormat()].Name : TEXT("none"), Dest ? (int32)Dest->GetSizeX() : 0, Dest ? (int32)Dest->GetSizeY() : 0, R.Min.X, R.Min.Y, R.Max.X, R.Max.Y);
				int32 Expected = static_cast<int32>(FACFTransitionGpuCapture::EState::Pending);
				Shared->State.compare_exchange_strong(Expected, static_cast<int32>(FACFTransitionGpuCapture::EState::Failed));
				return;
			}
			FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
			FRHICopyTextureInfo Info;
			Info.Size = FIntVector(R.Width(), R.Height(), 1);
			Info.SourcePosition = FIntVector(R.Min.X, R.Min.Y, 0);
			TransitionAndCopyTexture(RHICmdList, BackBuffer, Dest, Info);
			int32 Expected = static_cast<int32>(FACFTransitionGpuCapture::EState::Pending);
			Shared->State.compare_exchange_strong(Expected, static_cast<int32>(FACFTransitionGpuCapture::EState::Ready)); // stays Failed when cancelled meanwhile
		});
	}
	GLastTiming = FCaptureTiming();
	GLastTiming.bGpuCopy = true;
	GLastTiming.GameThreadMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	GLastTiming.Width = Rect.Width();
	GLastTiming.Height = Rect.Height();
	return Capture;
}

FACFTransitionGpuCapture::FACFTransitionGpuCapture()
{
	++ACFTransitionCapture::GLiveGpuCaptures;
}

FACFTransitionGpuCapture::~FACFTransitionGpuCapture()
{
	Cancel();
	--ACFTransitionCapture::GLiveGpuCaptures;
}

int32 FACFTransitionGpuCapture::GetLiveCount()
{
	return ACFTransitionCapture::GLiveGpuCaptures;
}

int32 FACFTransitionGpuCapture::GetListenerCount()
{
	return ACFTransitionCapture::GListeners;
}

bool FACFTransitionGpuCapture::MatchesViewport(UGameViewportClient* ViewportClient) const
{
	FIntRect Now;
	return FACFTransitionView::GetViewportPixelRect(ViewportClient, Now) && Now == RequestRect;
}

FACFTransitionGpuCapture::EState FACFTransitionGpuCapture::GetState() const
{
	return Shared ? static_cast<EState>(Shared->State.load()) : EState::Failed;
}

UTexture* FACFTransitionGpuCapture::GetTexture() const
{
	return Target;
}

uint64 FACFTransitionGpuCapture::GetFramesWaited() const
{
	return GFrameCounter - RequestFrame;
}

void FACFTransitionGpuCapture::Cancel()
{
	// The listener holds only a weak pointer to Shared: after Remove (thread-safe delegate) a call that is already running
	// on the render thread finishes with the resource that Target still keeps; later presents do not call it.
	if (Handle.IsValid())
	{
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
		{
			FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(Handle);
		}
		--ACFTransitionCapture::GListeners;
	}
	Handle.Reset();
	if (Shared && Shared->State.load() == static_cast<int32>(EState::Pending))
	{
		int32 Expected = static_cast<int32>(EState::Pending);
		Shared->State.compare_exchange_strong(Expected, static_cast<int32>(EState::Failed)); // a listener that has not started yet does not copy
	}
}

void FACFTransitionGpuCapture::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Target);
}

UTexture2D* FACFTransitionView::CreatePreviewPicture(int32 W, int32 H)
{
	TArray<FColor> P;
	P.SetNumUninitialized(W * H);
	static const FLinearColor Cols[5] = { {0.9f, 0.3f, 0.1f}, {0.95f, 0.8f, 0.1f}, {0.6f, 0.2f, 0.7f}, {0.9f, 0.9f, 0.9f}, {0.2f, 0.7f, 0.9f} };
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			const float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
			FLinearColor C = v < 0.6f ? FMath::Lerp(FLinearColor(0.25f, 0.45f, 0.85f), FLinearColor(0.75f, 0.85f, 0.95f), v / 0.6f)
				: FMath::Lerp(FLinearColor(0.35f, 0.55f, 0.25f), FLinearColor(0.2f, 0.35f, 0.15f), (v - 0.6f) / 0.4f);
			if (FMath::Square(u - 0.8f) * 3.2f + FMath::Square(v - 0.2f) < 0.006f) { C = FLinearColor(1.f, 0.9f, 0.5f); }
			for (int32 k = 0; k < 5; ++k)
			{
				const float bx = 0.1f + k * 0.18f, bw = 0.06f + 0.02f * (k % 2), top = 0.52f - 0.08f * (k % 3);
				if (u > bx && u < bx + bw && v > top && v < 0.66f) { C = Cols[k] * (0.7f + 0.3f * (u - bx) / bw); }
			}
			if (((x * 16 / W) + (y * 9 / H)) % 2 == 0 && v > 0.66f) { C *= 0.85f; }
			P[y * W + x] = C.ToFColor(true);
		}
	}
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("ACFTransitionPreviewPicture"));
	if (!T)
	{
		return nullptr;
	}
	T->SRGB = true;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, P.GetData(), P.Num() * sizeof(FColor));
	T->GetPlatformData()->Mips[0].BulkData.Unlock();
	T->UpdateResource();
	return T;
}
