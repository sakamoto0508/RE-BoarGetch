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
// LoadoutEntryが参照するボタン名とラベル名を使い、写真・バッジ・発光枠を持つカードを構築します。
TSharedRef<SWidget> UBoarArchiveCard::RebuildWidget()
{
 auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(206);Size->SetHeightOverride(242);WidgetTree->RootWidget=Size;
 auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),ButtonName);Size->SetContent(Button);
 FButtonStyle Style=Button->GetStyle();Style.Normal.TintColor=FSlateColor(FLinearColor(0.008f,0.026f,0.05f));Style.Hovered.TintColor=FSlateColor(FLinearColor(0.03f,0.08f,0.12f));Style.Pressed.TintColor=FSlateColor(FLinearColor(0.1f,0.14f,0.12f));Button->SetStyle(Style);
 auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Button->SetContent(Layers);
 Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetPadding(FMargin(4));Layers->AddChildToOverlay(Frame);
 auto* Inner=WidgetTree->ConstructWidget<UBorder>();Inner->SetBrushColor(FLinearColor(0.006f,0.02f,0.044f));Inner->SetPadding(FMargin(12));Frame->SetContent(Inner);
 auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Inner->SetContent(Column);
 Portrait=WidgetTree->ConstructWidget<UImage>();auto* PhotoSize=WidgetTree->ConstructWidget<USizeBox>();PhotoSize->SetHeightOverride(110);PhotoSize->SetContent(Portrait);Column->AddChildToVerticalBox(PhotoSize);
 auto MakeText=[&](FName Name,int32 FontSize){auto* T=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);auto F=T->GetFont();F.Size=FontSize;T->SetFont(F);T->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f,0.94f,1)));T->SetAutoWrapText(true);Column->AddChildToVerticalBox(T)->SetPadding(FMargin(0,4));return T;};
 Placeholder=MakeText(TEXT("PortraitStatus"),16);MakeText(LabelName,21);BadgeText=MakeText(TEXT("DisplayBadge"),15);
 GlowFrame=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("FocusFrame"));
 GlowFrame->SetVisibility(ESlateVisibility::HitTestInvisible);Layers->AddChildToOverlay(GlowFrame);
 if(FlowGlowMaterial)GlowFrame->SetBrushFromMaterial(FlowGlowMaterial);
 RefreshFrame();
 return Super::RebuildWidget();
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
 if(Frame)Frame->SetBrushColor(bActiveCard||bFocusedCard?FLinearColor(1,0.66f,0.08f):FLinearColor(0.06f,0.54f,0.75f));
 if(GlowFrame && FlowGlowMaterial)
 {
  if(auto* MID=GlowFrame->GetDynamicMaterial())MID->SetVectorParameterValue(TEXT("ActiveTint"),FLinearColor(1,0.66f,0.08f));
  UBoarFlowGlowLibrary::ResizeFlowFrame(this,FVector2D(206,242));
  UBoarFlowGlowLibrary::SetFlowFrameState(this,true,bActiveCard||bFocusedCard);
 }
 SetRenderScale(bFocusedCard?FVector2D(1.035f):FVector2D(1));
}
void UBoarArchiveCard::NativeOnAddedToFocusPath(const FFocusEvent& E){bFocusedCard=true;RefreshFrame();Super::NativeOnAddedToFocusPath(E);}
void UBoarArchiveCard::NativeOnRemovedFromFocusPath(const FFocusEvent& E){bFocusedCard=false;RefreshFrame();Super::NativeOnRemovedFromFocusPath(E);}
