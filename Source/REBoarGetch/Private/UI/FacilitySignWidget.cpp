#include "UI/FacilitySignWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"

TSharedRef<SWidget> UFacilitySignWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        // 文字面はパネル中央に置く。Overlayで上下左右とも中央寄せにする。
        auto* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
        WidgetTree->RootWidget = Root;
        auto* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Lines"));
        UOverlaySlot* BoxSlot = Root->AddChildToOverlay(Box);
        BoxSlot->SetHorizontalAlignment(HAlign_Center);
        BoxSlot->SetVerticalAlignment(VAlign_Center);
        TitleBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
        SubtitleBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Subtitle"));
        for (UTextBlock* Block : {TitleBlock.Get(), SubtitleBlock.Get()})
        {
            UVerticalBoxSlot* LineSlot = Box->AddChildToVerticalBox(Block);
            LineSlot->SetHorizontalAlignment(HAlign_Center);
            Block->SetJustification(ETextJustify::Center);
        }
    }
    return Super::RebuildWidget();
}

void UFacilitySignWidget::SetContent(const FText& Title, const FText& Subtitle, const FSlateFontInfo& TitleFont, const FSlateFontInfo& SubtitleFont,
                                     const FLinearColor& TitleColor, const FLinearColor& SubtitleColor, float GapPx)
{
    if (!TitleBlock || !SubtitleBlock)
        return;
    TitleBlock->SetFont(TitleFont);
    TitleBlock->SetColorAndOpacity(FSlateColor(TitleColor));
    TitleBlock->SetText(Title);
    TitleBlock->SetVisibility(Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    SubtitleBlock->SetFont(SubtitleFont);
    SubtitleBlock->SetColorAndOpacity(FSlateColor(SubtitleColor));
    SubtitleBlock->SetText(Subtitle);
    SubtitleBlock->SetVisibility(Subtitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (auto* LineSlot = Cast<UVerticalBoxSlot>(SubtitleBlock->Slot))
        LineSlot->SetPadding(FMargin(0.f, GapPx, 0.f, 0.f));
}
