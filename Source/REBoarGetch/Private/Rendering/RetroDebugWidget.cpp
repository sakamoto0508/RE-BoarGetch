#include "Rendering/RetroDebugWidget.h"
#include "Rendering/RetroCompareActor.h"
#include "Rendering/RetroRenderComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

namespace
{
 void SetTextSize(UTextBlock* Text,int32 Size)
 { auto Font=Text->GetFont(); Font.Size=Size; Text->SetFont(Font); }
}
TSharedRef<SWidget> URetroStatusWidget::RebuildWidget()
{
 if (!WidgetTree->RootWidget)
 {
  auto* Root=WidgetTree->ConstructWidget<UOverlay>(); WidgetTree->RootWidget=Root;
  Label=WidgetTree->ConstructWidget<UTextBlock>(); SetTextSize(Label,14);
  Label->SetColorAndOpacity(FSlateColor(FLinearColor(.3f,1.f,1.f)));
  auto* StatusSlot=Root->AddChildToOverlay(Label); StatusSlot->SetHorizontalAlignment(HAlign_Left);
  StatusSlot->SetVerticalAlignment(VAlign_Bottom); StatusSlot->SetPadding(FMargin(14));
 }
 return Super::RebuildWidget();
}
void URetroStatusWidget::Refresh(URetroRenderComponent* Renderer)
{
 if (Label && Renderer) Label->SetText(FText::FromString(FString::Printf(TEXT("Retro: %s  [Ctrl+Alt+F7]"),*RetroPresetName(Renderer->GetPreset()))));
}

void URetroParameterRow::Configure(URetroRenderComponent* InRenderer,int32 Index)
{ Renderer=InRenderer; ParameterIndex=Index; }
TSharedRef<SWidget> URetroParameterRow::RebuildWidget()
{
 if (WidgetTree->RootWidget) return Super::RebuildWidget();
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget=Box;
 Label=WidgetTree->ConstructWidget<UTextBlock>(); Box->AddChild(Label);
 SetTextSize(Label,16);
 Slider=WidgetTree->ConstructWidget<USlider>(); Box->AddChild(Slider);
 const auto& D=GetRetroParameterDescriptors()[ParameterIndex];
 Slider->SetMinValue(D.Min); Slider->SetMaxValue(D.Max);
 Slider->SetStepSize((D.Max-D.Min)/200.f);
 Refresh(); Slider->OnValueChanged.AddUniqueDynamic(this,&ThisClass::Changed);
 return Super::RebuildWidget();
}
void URetroParameterRow::NativeConstruct() { Super::NativeConstruct(); Refresh(); }
void URetroParameterRow::Refresh()
{
 if (!Renderer || !Label || !Slider) return;
 const auto& D=GetRetroParameterDescriptors()[ParameterIndex];
 const float Value=Renderer->GetParameters().*(D.Member);
 Label->SetText(FText::FromString(FString::Printf(TEXT("%s : %.3f"),D.Name,Value)));
 Slider->SetValue(Value);
 Slider->SetIsEnabled(Renderer->GetPreset()!=ERetroRenderPreset::OFF);
}
void URetroParameterRow::Changed(float Value)
{
 if (!Renderer) return;
 Renderer->SetParameter(GetRetroParameterDescriptors()[ParameterIndex].Name,Value); Refresh();
}
TSharedRef<SWidget> URetroDebugWidget::RebuildWidget()
{
 if (WidgetTree->RootWidget) return Super::RebuildWidget();
 SetIsFocusable(true);
 auto* Root=WidgetTree->ConstructWidget<UOverlay>(); WidgetTree->RootWidget=Root;
 auto* Size=WidgetTree->ConstructWidget<USizeBox>();
 Size->SetWidthOverride(480);
 auto* PanelSlot=Root->AddChildToOverlay(Size);
 PanelSlot->SetHorizontalAlignment(HAlign_Left); PanelSlot->SetVerticalAlignment(VAlign_Fill); PanelSlot->SetPadding(FMargin(12));
 auto* Background=WidgetTree->ConstructWidget<UBorder>(); Size->AddChild(Background);
 Background->SetBrushColor(FLinearColor(.018f,.025f,.04f,.94f)); Background->SetPadding(FMargin(10));
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); Background->AddChild(Box);
 Title=WidgetTree->ConstructWidget<UTextBlock>(); Box->AddChild(Title);
 SetTextSize(Title,16);
 auto* Bar=WidgetTree->ConstructWidget<UHorizontalBox>(); Box->AddChild(Bar);
 auto MakeButton=[this](UHorizontalBox* Parent,const TCHAR* Text)
 {
  auto* Button=WidgetTree->ConstructWidget<UButton>();
  auto* Label=WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(FText::FromString(Text));
  SetTextSize(Label,16);
  Button->AddChild(Label); Parent->AddChildToHorizontalBox(Button)->SetPadding(FMargin(6)); return Button;
 };
 MakeButton(Bar,TEXT("OFF"))->OnClicked.AddDynamic(this,&ThisClass::Off);
 MakeButton(Bar,TEXT("PS1"))->OnClicked.AddDynamic(this,&ThisClass::PS1);
 MakeButton(Bar,TEXT("N64"))->OnClicked.AddDynamic(this,&ThisClass::N64);
 MakeButton(Bar,TEXT("PS2"))->OnClicked.AddDynamic(this,&ThisClass::PS2);
 MakeButton(Bar,TEXT("CRT"))->OnClicked.AddDynamic(this,&ThisClass::CRT);
 auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>(); Box->AddChild(Actions);
 MakeButton(Actions,TEXT("Reset"))->OnClicked.AddDynamic(this,&ThisClass::Reset);
 MakeButton(Actions,TEXT("Screenshot"))->OnClicked.AddDynamic(this,&ThisClass::Capture);
 MakeButton(Actions,TEXT("Compare 5"))->OnClicked.AddDynamic(this,&ThisClass::Compare);
 MakeButton(Actions,TEXT("Close"))->OnClicked.AddDynamic(this,&ThisClass::Close);
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
 Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 Rows.Reset();
 for (int32 I=0;I<GetRetroParameterDescriptors().Num();++I)
 {
  auto* Row=CreateWidget<URetroParameterRow>(GetOwningPlayer());
  Row->Configure(Harness->Renderer,I); Rows.Add(Row); Scroll->AddChild(Row);
 }
 Refresh();
 return Super::RebuildWidget();
}
void URetroDebugWidget::NativeConstruct() { Super::NativeConstruct(); Refresh(); }
void URetroDebugWidget::Refresh()
{
 if (!Harness || !Title) return;
 Title->SetText(FText::FromString(FString::Printf(TEXT("Retro: %s | Ctrl+Alt+F7 | %s"),
  *RetroPresetName(Harness->Renderer->GetPreset()),Harness->Renderer->IsReady()?TEXT("Ready"):TEXT("MATERIAL MISSING"))));
 for (const auto& Row:Rows) Row->Refresh();
}
FReply URetroDebugWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
 if (Event.GetKey()==EKeys::F7 && Event.IsControlDown() && Event.IsAltDown())
 { Close(); return FReply::Handled(); }
 return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
void URetroDebugWidget::Select(uint8 Index) { if (Harness) { Harness->Renderer->SetPreset(ERetroRenderPreset(Index)); Refresh(); } }
void URetroDebugWidget::Off(){Select(0);}
void URetroDebugWidget::PS1(){Select(1);}
void URetroDebugWidget::N64(){Select(2);}
void URetroDebugWidget::PS2(){Select(3);}
void URetroDebugWidget::CRT(){Select(4);}
void URetroDebugWidget::Reset(){if(Harness){Harness->Renderer->ResetCurrentPreset();Refresh();}}
void URetroDebugWidget::Capture(){if(Harness)Harness->CaptureCurrent();}
void URetroDebugWidget::Compare(){if(Harness)Harness->CaptureAll();}
void URetroDebugWidget::Close(){if(Harness)Harness->ToggleDebugUI();}
