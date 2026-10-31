#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FacilitySignWidget.generated.h"

class UTextBlock;

/** AFacilitySignの文字面。Widget Blueprintを持たずC++で2行構成を組み、既定フォントのFallbackで日本語も表示します。 */
UCLASS()
class REBOARGETCH_API UFacilitySignWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    /** フォントのSizeはpt、GapPxは2行間の余白（px）。 */
    void SetContent(const FText& Title, const FText& Subtitle, const FSlateFontInfo& TitleFont, const FSlateFontInfo& SubtitleFont,
                    const FLinearColor& TitleColor, const FLinearColor& SubtitleColor, float GapPx);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleBlock;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleBlock;
};
