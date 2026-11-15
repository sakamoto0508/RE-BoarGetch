#include "ACFTransitionView.h"

#include "ACFTransitionCatalog.h"
#include "ACFTransitionSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
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
		MID->SetScalarParameterValue(UseSnapshot, Snapshot ? 1.f : 0.f);
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

UTexture2D* FACFTransitionView::CaptureViewport(UGameViewportClient* ViewportClient, int32 MaxWidth)
{
	if (!ViewportClient || GUsingNullRHI || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
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
	if (!FSlateApplication::Get().TakeScreenshot(ViewportWidget.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() != Size.X * Size.Y)
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
	UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("ACFTransitionCapture"));
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
	return Texture;
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
