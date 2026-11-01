#include "UI/FacilitySignWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

UFacilitySignWidget::UFacilitySignWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // 既存サインの見た目（旧StyleFor）を初期値にしています。調整はWBP_FacilitySignのClass Defaultsで行います。
    const FLinearColor White(FColor(244, 248, 252)), Cyan(FColor(64, 212, 240)), Yellow(FColor(255, 204, 51));
    auto Make = [](float Title, float Sub, float Pad, float Intensity, float Depth, FLinearColor TitleColor, FLinearColor SubColor)
    {
        FFacilitySignStyle Style;
        Style.TitleSize = Title;
        Style.SubtitleSize = Sub;
        Style.Padding = Pad;
        Style.Intensity = Intensity;
        Style.Depth = Depth;
        Style.TitleColor = TitleColor;
        Style.SubtitleColor = SubColor;
        return Style;
    };
    TierStyles.Add(EFacilitySignTier::AreaSign, Make(60.f, 16.f, 36.f, 3.f, 12.f, White, Cyan));
    TierStyles.Add(EFacilitySignTier::FacilityLabel, Make(14.f, 7.f, 12.f, 1.5f, 4.f, White, Cyan));
    TierStyles.Add(EFacilitySignTier::Nameplate, Make(9.f, 5.f, 6.f, 1.f, 2.f, White, Cyan));
    TierStyles.Add(EFacilitySignTier::Prompt, Make(7.f, 5.f, 6.f, 4.f, 1.f, Yellow, Yellow));
}

FFacilitySignStyle UFacilitySignWidget::GetStyle(EFacilitySignTier Tier) const
{
    const FFacilitySignStyle* Style = TierStyles.Find(Tier);
    return Style ? *Style : FFacilitySignStyle();
}

FSlateFontInfo UFacilitySignWidget::MakeTitleFont(int32 Points) const
{
    FSlateFontInfo Font = TitleText ? TitleText->GetFont() : FCoreStyle::GetDefaultFontStyle("Bold", Points);
    Font.Size = Points;
    return Font;
}

FSlateFontInfo UFacilitySignWidget::MakeSubtitleFont(int32 Points) const
{
    FSlateFontInfo Font = SubtitleText ? SubtitleText->GetFont() : FCoreStyle::GetDefaultFontStyle("Regular", Points);
    Font.Size = Points;
    return Font;
}

void UFacilitySignWidget::SetContent(const FText& Title, const FText& Subtitle, const FSlateFontInfo& TitleFont, const FSlateFontInfo& SubtitleFont,
                                     const FLinearColor& TitleColor, const FLinearColor& SubtitleColor, float GapPx)
{
    if (TitleText)
    {
        TitleText->SetFont(TitleFont);
        TitleText->SetColorAndOpacity(FSlateColor(TitleColor));
        TitleText->SetText(Title);
        TitleText->SetVisibility(Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
    if (SubtitleText)
    {
        SubtitleText->SetFont(SubtitleFont);
        SubtitleText->SetColorAndOpacity(FSlateColor(SubtitleColor));
        SubtitleText->SetText(Subtitle);
        SubtitleText->SetVisibility(Subtitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
        if (auto* LineSlot = Cast<UVerticalBoxSlot>(SubtitleText->Slot))
            LineSlot->SetPadding(FMargin(0.f, GapPx, 0.f, 0.f));
    }
}
