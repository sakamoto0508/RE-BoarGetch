#include "UI/BoarArchiveCard.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Components/Overlay.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/BoarFlowGlowLibrary.h"
UBoarArchiveCard::UBoarArchiveCard(const FObjectInitializer& O):Super(O)
{
 ButtonName=TEXT("CardButton");LabelName=TEXT("CardLabel");
}
// WBPでレイアウトを作成済みならBindWidgetOptionalの参照をそのまま使います。
TSharedRef<SWidget> UBoarArchiveCard::RebuildWidget()
{
 if(WidgetTree && !WidgetTree->RootWidget)BuildFallbackLayout();
 if(FocusFrame && FlowGlowMaterial)FocusFrame->SetBrushFromMaterial(FlowGlowMaterial);
 RefreshFrame();
 return Super::RebuildWidget();
}
// WBPにレイアウトが無い場合の仮表示です。LoadoutEntryが参照するボタン名とラベル名で構築します。
void UBoarArchiveCard::BuildFallbackLayout()
{
 auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(CardSize.X);Size->SetHeightOverride(CardSize.Y);WidgetTree->RootWidget=Size;
 auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),ButtonName);Size->SetContent(Button);
 FButtonStyle Style=Button->GetStyle();Style.Normal.TintColor=FSlateColor(FLinearColor(0.008f,0.026f,0.05f));Style.Hovered.TintColor=FSlateColor(FLinearColor(0.03f,0.08f,0.12f));Style.Pressed.TintColor=FSlateColor(FLinearColor(0.1f,0.14f,0.12f));Button->SetStyle(Style);
 auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Button->SetContent(Layers);
 Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetPadding(FMargin(4));Layers->AddChildToOverlay(Frame);
 auto* Inner=WidgetTree->ConstructWidget<UBorder>();Inner->SetBrushColor(FLinearColor(0.006f,0.02f,0.044f));Inner->SetPadding(FMargin(12));Frame->SetContent(Inner);
 auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Inner->SetContent(Column);
 Portrait=WidgetTree->ConstructWidget<UImage>();auto* PhotoSize=WidgetTree->ConstructWidget<USizeBox>();PhotoSize->SetHeightOverride(110);PhotoSize->SetContent(Portrait);Column->AddChildToVerticalBox(PhotoSize);
 auto MakeText=[&](FName Name,int32 FontSize){auto* T=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);auto F=T->GetFont();F.Size=FontSize;T->SetFont(F);T->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f,0.94f,1)));T->SetAutoWrapText(true);Column->AddChildToVerticalBox(T)->SetPadding(FMargin(0,4));return T;};
 Placeholder=MakeText(TEXT("Placeholder"),16);MakeText(LabelName,21);BadgeText=MakeText(TEXT("BadgeText"),15);
 FocusFrame=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("FocusFrame"));
 FocusFrame->SetVisibility(ESlateVisibility::HitTestInvisible);Layers->AddChildToOverlay(FocusFrame);
}
// 写真がない場合は代替文言を表示し、展示枠の選択状態とバッジを描画へ反映します。
void UBoarArchiveCard::SetPortrait(UTexture2D* Texture,const FText& Caption,const FText& Badge,bool bActive)
{
 if(Portrait){Portrait->SetBrushFromTexture(Texture);Portrait->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);}
 if(Placeholder)Placeholder->SetText(Texture?FText::GetEmpty():Caption);
 if(BadgeText)BadgeText->SetText(Badge);
 bActiveCard=bActive;RefreshFrame();
}
// 編集中の展示枠とフォーカス中のカードを強調します。展示の保存や割り当て処理は親画面が担当します。
void UBoarArchiveCard::RefreshFrame()
{
 const bool bHighlighted=bActiveCard||bFocusedCard;
 if(Frame)Frame->SetBrushColor(bHighlighted?ActiveColor:IdleColor);
 if(FocusFrame && FlowGlowMaterial)
 {
  if(auto* MID=FocusFrame->GetDynamicMaterial())MID->SetVectorParameterValue(TEXT("ActiveTint"),ActiveColor);
  UBoarFlowGlowLibrary::ResizeFlowFrame(this,CardSize);
  UBoarFlowGlowLibrary::SetFlowFrameState(this,true,bHighlighted);
 }
 SetRenderScale(bFocusedCard?FVector2D(1.035f):FVector2D(1));
}
void UBoarArchiveCard::NativeOnAddedToFocusPath(const FFocusEvent& E){bFocusedCard=true;RefreshFrame();Super::NativeOnAddedToFocusPath(E);}
void UBoarArchiveCard::NativeOnRemovedFromFocusPath(const FFocusEvent& E){bFocusedCard=false;RefreshFrame();Super::NativeOnRemovedFromFocusPath(E);}
