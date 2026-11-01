#include "UI/BoarArchiveCard.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/BoarFlowGlowLibrary.h"

UBoarArchiveCard::UBoarArchiveCard(const FObjectInitializer& O) : Super(O)
{
	ButtonName = TEXT("CardButton");
	LabelName = TEXT("CardLabel");
}

// WBPのレイアウトとBindWidgetOptionalの参照をそのまま使います。C++で代替レイアウトは作りません。
TSharedRef<SWidget> UBoarArchiveCard::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
		UE_LOG(LogTemp, Error, TEXT("[Archive] %s: WBP has no layout (RootWidget is empty). Card is not displayed."),
		       *GetNameSafe(GetClass()));
	if (FocusFrame && FlowGlowMaterial)
		FocusFrame->SetBrushFromMaterial(FlowGlowMaterial);
	RefreshFrame();
	return Super::RebuildWidget();
}

// 写真がない場合は代替文言を表示し、展示枠の選択状態とバッジを描画へ反映します。
void UBoarArchiveCard::SetPortrait(UTexture2D* Texture, const FText& Caption, const FText& Badge, bool bActive)
{
	if (Portrait)
	{
		Portrait->SetBrushFromTexture(Texture);
		Portrait->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (Placeholder)
		Placeholder->SetText(Texture ? FText::GetEmpty() : Caption);
	if (BadgeText)
		BadgeText->SetText(Badge);
	bActiveCard = bActive;
	RefreshFrame();
}

// 編集中の展示枠とフォーカス中のカードを強調します。展示の保存や割り当て処理は親画面が担当します。
void UBoarArchiveCard::RefreshFrame()
{
	const bool bHighlighted = bActiveCard || bFocusedCard;
	if (Frame)
		Frame->SetBrushColor(bHighlighted ? ActiveColor : IdleColor);
	if (FocusFrame && FlowGlowMaterial)
	{
		if (auto* MID = FocusFrame->GetDynamicMaterial())
			MID->SetVectorParameterValue(TEXT("ActiveTint"), ActiveColor);
		UBoarFlowGlowLibrary::ResizeFlowFrame(this, CardSize);
		UBoarFlowGlowLibrary::SetFlowFrameState(this, true, bHighlighted);
	}
	SetRenderScale(bFocusedCard ? FVector2D(1.035f) : FVector2D(1));
}

void UBoarArchiveCard::NativeOnAddedToFocusPath(const FFocusEvent& E)
{
	bFocusedCard = true;
	RefreshFrame();
	Super::NativeOnAddedToFocusPath(E);
}

void UBoarArchiveCard::NativeOnRemovedFromFocusPath(const FFocusEvent& E)
{
	bFocusedCard = false;
	RefreshFrame();
	Super::NativeOnRemovedFromFocusPath(E);
}
