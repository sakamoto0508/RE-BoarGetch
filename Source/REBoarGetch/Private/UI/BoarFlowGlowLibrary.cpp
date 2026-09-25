#include "UI/BoarFlowGlowLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
UMaterialInstanceDynamic* FlowMaterial(UUserWidget* Owner)
{
	UImage* Image = Owner && Owner->WidgetTree ? Cast<UImage>(Owner->WidgetTree->FindWidget(TEXT("FocusFrame"))) : nullptr;
	if (!Image) return nullptr;
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Image->GetDynamicMaterial();
}
}
void UBoarFlowGlowLibrary::ResizeFlowFrame(UUserWidget* Owner, FVector2D LocalSize)
{
	if (LocalSize.X <= 0 || LocalSize.Y <= 0) return;
	if (auto* MID = FlowMaterial(Owner))
	{
		// Tick only observes geometry; no per-frame parameter writes or flow animation.
		if (!FMath::IsNearlyEqual(MID->K2_GetScalarParameterValue(TEXT("Width")), LocalSize.X)) MID->SetScalarParameterValue(TEXT("Width"), LocalSize.X);
		if (!FMath::IsNearlyEqual(MID->K2_GetScalarParameterValue(TEXT("Height")), LocalSize.Y)) MID->SetScalarParameterValue(TEXT("Height"), LocalSize.Y);
	}
}
void UBoarFlowGlowLibrary::SetFlowFrameState(UUserWidget* Owner, bool bFocused, bool bValue)
{
	if (auto* MID = FlowMaterial(Owner))
	{
		MID->SetScalarParameterValue(bFocused ? TEXT("Focused") : TEXT("Hovered"), bValue ? 1.f : 0.f);
		const bool bActive = MID->K2_GetScalarParameterValue(TEXT("Focused")) > .5f || MID->K2_GetScalarParameterValue(TEXT("Hovered")) > .5f;
		MID->SetScalarParameterValue(TEXT("Active"), bActive ? 1.f : 0.f);
		Owner->SetRenderScale(FVector2D(bActive ? 1.035f : 1.f));
	}
}
