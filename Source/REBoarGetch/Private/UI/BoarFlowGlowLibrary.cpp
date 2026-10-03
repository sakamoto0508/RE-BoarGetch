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
// FocusFrameのMaterialへWidgetの寸法を渡し、表示サイズが変わったときだけ更新します。
void UBoarFlowGlowLibrary::ResizeFlowFrame(UUserWidget* Owner, FVector2D LocalSize)
{
	if (LocalSize.X <= 0 || LocalSize.Y <= 0) return;
	if (auto* MID = FlowMaterial(Owner))
	{
		// Tickから呼ばれても寸法変更時だけ書き込みます。流れる発光のアニメーションはMaterial側で行います。
		if (!FMath::IsNearlyEqual(MID->K2_GetScalarParameterValue(TEXT("Width")), LocalSize.X)) MID->SetScalarParameterValue(TEXT("Width"), LocalSize.X);
		if (!FMath::IsNearlyEqual(MID->K2_GetScalarParameterValue(TEXT("Height")), LocalSize.Y)) MID->SetScalarParameterValue(TEXT("Height"), LocalSize.Y);
	}
}
// フォーカスとホバーのどちらかが有効なら発光を有効化し、Widgetの拡大表示も更新します。
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
