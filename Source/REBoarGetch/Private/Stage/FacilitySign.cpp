#include "Stage/FacilitySign.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/Font.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Stage/HubPortal.h"
#include "Archive/BoarArchiveDisplay.h"
#include "UI/FacilitySignWidget.h"

namespace
{
	// 全World Space表示の共通デザイン言語。サイズ単位はcm（文字の高さ）、IntensityはM_FacilityUI_Widgetの発光倍率。
	struct FSignStyle
	{
		float TitleSize;
		float SubtitleSize;
		float Padding;
		float Intensity;
		float Depth;
		FLinearColor TitleColor;
		FLinearColor SubtitleColor;
	};

	const FLinearColor White(FColor(244, 248, 252)), Cyan(FColor(64, 212, 240)), Yellow(FColor(255, 204, 51));

	FSignStyle StyleFor(EFacilitySignTier Tier)
	{
		switch (Tier)
		{
		case EFacilitySignTier::AreaSign: return {60.f, 16.f, 36.f, 3.f, 12.f, White, Cyan};
		case EFacilitySignTier::Nameplate: return {9.f, 5.f, 6.f, 1.f, 2.f, White, Cyan};
		case EFacilitySignTier::Prompt: return {7.f, 5.f, 6.f, 4.f, 1.f, Yellow, Yellow};
		default: return {14.f, 7.f, 12.f, 1.5f, 4.f, White, Cyan};
		}
	}

	// 文字の描画解像度。どの階層でもタイトル1文字が約64pxになるよう、1cmあたりのpx数を決める。
	float PixelsPerCm(const FSignStyle& Style) { return FMath::Clamp(64.f / Style.TitleSize, 1.f, 8.f); }
	// Slateのフォントサイズはpt（96dpiで1pt=4/3px）。文字の高さ（em）をpxからptへ変換する。
	int32 FontPoints(float SizeCm, float Ppc) { return FMath::Max(1, FMath::RoundToInt(SizeCm * Ppc * 0.75f)); }

	// 幅（cm）。Slateが使える環境では実測し、Commandlet等では和文を全角、欧文を0.62emとして推定する。
	float MeasureWidth(const FText& Value, float SizeCm, float Ppc, const FSlateFontInfo& Font, float LetterSpacingEm)
	{
		const FString String = Value.ToString();
		if (String.IsEmpty())return 0.f;
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, Font).X / Ppc;
		float Em = 0.f;
		for (const TCHAR Char : String)Em += (Char >= 0x2E80 ? 1.f : 0.62f) + LetterSpacingEm;
		return Em * SizeCm;
	}

	void SetMeshMaterial(AActor* Actor, UMaterialInterface* Material)
	{
		if (!Actor || !Material)return;
		for (UActorComponent* Component : Actor->GetComponents())
			if (auto* Mesh = Cast<UStaticMeshComponent>(Component))Mesh->SetMaterial(0, Material);
	}
}

AFacilitySign::AFacilitySign()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UFont> SignFont(
		TEXT("/Game/InportAssets/Font/Dela_Gothic_One/DelaGothicOne-Regular_Font.DelaGothicOne-Regular_Font"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WidgetMat(
		TEXT("/Game/UI/World/M_FacilityUI_Widget.M_FacilityUI_Widget"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PanelMat(
		TEXT("/Game/UI/World/MI_FacilityUI_Panel.MI_FacilityUI_Panel"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FrameMat(
		TEXT("/Game/UI/World/MI_FacilityUI_Frame.MI_FacilityUI_Frame"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LineIdle(
		TEXT("/Game/UI/World/MI_FacilityUI_LineIdle.MI_FacilityUI_LineIdle"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LineActive(
		TEXT("/Game/UI/World/MI_FacilityUI_LineActive.MI_FacilityUI_LineActive"));
	Font = SignFont.Object;
	WidgetMaterial = WidgetMat.Object;
	PanelMaterial = PanelMat.Object;
	AccentMaterial = FrameMat.Object;
	HighlightOffMaterial = LineIdle.Object;
	HighlightOnMaterial = LineActive.Object;

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	AccentLine = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AccentLine"));
	for (UStaticMeshComponent* Mesh : {Panel.Get(), AccentLine.Get()})
	{
		Mesh->SetupAttachment(SceneRoot);
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
	}
	LabelWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("LabelWidget"));
	LabelWidget->SetupAttachment(SceneRoot);
	LabelWidget->SetWidgetSpace(EWidgetSpace::World);
	LabelWidget->SetWidgetClass(UFacilitySignWidget::StaticClass());
	LabelWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LabelWidget->SetCanEverAffectNavigation(false);
	LabelWidget->SetCastShadow(false);
	LabelWidget->SetTwoSided(false);
	LabelWidget->SetDrawAtDesiredSize(false);
	LabelWidget->SetPivot(FVector2D(0.5f, 0.5f));
	LabelWidget->SetBackgroundColor(FLinearColor::Transparent);
}

void AFacilitySign::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (MirrorTextSource && MirrorTextSource->GetTextRender())
		MirrorTextSource->GetTextRender()->SetVisibility(false);
	const FText Shown = MirrorTextSource
		                    ? ReadMirrorText()
		                    : SpecimenSource && Title.IsEmpty()
		                    ? EmptyTitle
		                    : Title;
	ApplyLayout(Shown, false);
}

void AFacilitySign::ApplyLayout(const FText& NewTitle, bool bDimmed)
{
	ShownTitle = NewTitle;
	bShownDimmed = bDimmed;
	const FSignStyle Style = StyleFor(Tier);
	const float Ppc = PixelsPerCm(Style);
	const int32 TitlePt = FontPoints(Style.TitleSize, Ppc);
	const int32 SubtitlePt = FontPoints(Style.SubtitleSize, Ppc);
	FSlateFontInfo TitleFont = Font ? FSlateFontInfo(Font, TitlePt) : FCoreStyle::GetDefaultFontStyle("Bold", TitlePt);
	TitleFont.LetterSpacing = 80;
	FSlateFontInfo SubtitleFont = Font
		                              ? FSlateFontInfo(Font, SubtitlePt)
		                              : FCoreStyle::GetDefaultFontStyle("Regular", SubtitlePt);
	SubtitleFont.LetterSpacing = 120;

	// 行間と余白は文字高さから導出し、全サインで同じ比率にする。
	const bool bSubtitle = !Subtitle.IsEmpty();
	const float Gap = bSubtitle ? Style.Padding * 0.5f : 0.f;
	const float Total = Style.TitleSize + Gap + (bSubtitle ? Style.SubtitleSize : 0.f);
	const float Width = FixedPanelWidth > 0.f
		                    ? FixedPanelWidth
		                    : FMath::Max(
			                    MeasureWidth(NewTitle, Style.TitleSize, Ppc, TitleFont, 0.08f),
			                    bSubtitle ? MeasureWidth(Subtitle, Style.SubtitleSize, Ppc, SubtitleFont, 0.12f) : 0.f)
		                    + Style.Padding * 2.f;
	const float Height = Total + Style.Padding * 2.f;

	Panel->SetRelativeLocation(FVector(-Style.Depth * 0.5f, 0.f, 0.f));
	Panel->SetRelativeScale3D(FVector(Style.Depth, Width, Height) / 100.f);
	Panel->SetMaterial(0, PanelMaterial);
	const float LineHeight = FMath::Max(1.5f, Style.TitleSize * 0.06f);
	AccentLine->SetRelativeLocation(FVector(0.3f, 0.f, -Height * 0.5f + LineHeight * 0.5f));
	AccentLine->SetRelativeScale3D(FVector(0.6f, Width, LineHeight) / 100.f);
	AccentLine->SetMaterial(0, AccentMaterial);

	// 文字面はパネルと同じ大きさのWidgetを、1cmあたりPpc px で描いて縮小配置する。
	LabelWidget->SetRelativeLocation(FVector(0.5f, 0.f, 0.f));
	LabelWidget->SetRelativeScale3D(FVector(1.f, 1.f / Ppc, 1.f / Ppc));
	LabelWidget->SetDrawSize(FVector2D(Width * Ppc, Height * Ppc));
	if (WidgetMaterial)
		LabelWidget->SetMaterial(0, WidgetMaterial);
	LabelWidget->InitWidget();
	const float Dim = bDimmed ? 0.35f : 1.f;
	if (auto* Widget = Cast<UFacilitySignWidget>(LabelWidget->GetUserWidgetObject()))
		Widget->SetContent(NewTitle, Subtitle, TitleFont, SubtitleFont, Style.TitleColor * Dim,
		                   Style.SubtitleColor * Dim, Gap * Ppc);
	if (UMaterialInstanceDynamic* MID = LabelWidget->GetMaterialInstance())
		MID->SetScalarParameterValue(TEXT("Intensity"), Style.Intensity * Dim);
	LabelWidget->RequestRenderUpdate();
}

FText AFacilitySign::ReadMirrorText() const
{
	// TextRenderComponent::Textは直接参照できないため、反映Propertyから読む。
	const UTextRenderComponent* Text = MirrorTextSource ? MirrorTextSource->GetTextRender() : nullptr;
	const FTextProperty* Property = FindFProperty<FTextProperty>(UTextRenderComponent::StaticClass(), TEXT("Text"));
	return Text && Property ? Property->GetPropertyValue_InContainer(Text) : FText::GetEmpty();
}

void AFacilitySign::PollMirrorText()
{
	const FText Current = ReadMirrorText();
	if (!Current.EqualTo(ShownTitle))
		ApplyLayout(Current, false);
}

void AFacilitySign::BeginPlay()
{
	Super::BeginPlay();
	ApplyLayout(ShownTitle, bShownDimmed);
	if (ProximitySource && ProximitySource->Trigger)
	{
		ProximitySource->Trigger->OnComponentBeginOverlap.AddUniqueDynamic(this, &AFacilitySign::OnProximityBegin);
		ProximitySource->Trigger->OnComponentEndOverlap.AddUniqueDynamic(this, &AFacilitySign::OnProximityEnd);
	}
	if (ProximitySource || bShowOnlyWhenNear)
		SetNear(false);
	if (SpecimenSource)
	{
		SpecimenSource->OnDisplayRefreshed.AddUniqueDynamic(this, &AFacilitySign::RefreshSpecimen);
		RefreshSpecimen();
	}
	if (MirrorTextSource)
	{
		// Gameplay側の文字更新には通知が無いため、低頻度で差分だけ反映する。
		if (MirrorTextSource->GetTextRender())
			MirrorTextSource->GetTextRender()->SetVisibility(false);
		GetWorldTimerManager().SetTimer(MirrorTimer, this, &AFacilitySign::PollMirrorText, 0.2f, true, 0.f);
	}
}

void AFacilitySign::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ProximitySource && ProximitySource->Trigger)
	{
		ProximitySource->Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &AFacilitySign::OnProximityBegin);
		ProximitySource->Trigger->OnComponentEndOverlap.RemoveDynamic(this, &AFacilitySign::OnProximityEnd);
	}
	if (SpecimenSource)
		SpecimenSource->OnDisplayRefreshed.RemoveDynamic(this, &AFacilitySign::RefreshSpecimen);
	GetWorldTimerManager().ClearTimer(MirrorTimer);
	Super::EndPlay(Reason);
}

void AFacilitySign::SetNear(bool bNear)
{
	if (bShowOnlyWhenNear)
		SetActorHiddenInGame(!bNear);
	if (ProximitySource)
		SetHighlight(bNear);
}

void AFacilitySign::SetHighlight(bool bOn)
{
	for (AActor* Actor : HighlightActors)
		SetMeshMaterial(Actor, bOn ? HighlightOnMaterial : HighlightOffMaterial);
}

void AFacilitySign::RefreshSpecimen()
{
	if (!SpecimenSource)
		return;
	const bool bHas = SpecimenSource->HasSpecimen();
	const FText Name = SpecimenSource->GetDisplayedBoarName();
	ApplyLayout(!bHas ? EmptyTitle : Name.IsEmpty() ? UnnamedTitle : Name, !bHas);
	SetHighlight(bHas);
}

void AFacilitySign::OnProximityBegin(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool,
                                     const FHitResult&)
{
	const APawn* Pawn = Cast<APawn>(Other);
	if (Pawn && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled())
		SetNear(true);
}

void AFacilitySign::OnProximityEnd(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
	const APawn* Pawn = Cast<APawn>(Other);
	if (Pawn && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled())
		SetNear(false);
}
