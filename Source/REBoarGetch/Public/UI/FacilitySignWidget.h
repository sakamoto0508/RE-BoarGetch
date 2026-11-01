#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Stage/FacilitySign.h"
#include "FacilitySignWidget.generated.h"

class UTextBlock;

/**
 * AFacilitySignの文字面。WBP_FacilitySignを親として使い、文字の配置・書体・字間はDesigner、
 * 階層ごとの文字サイズ・色・余白・発光・パネル厚はClass Defaults（TierStyles）で調整します。
 * このクラスは受け取った文字と色・サイズを表示へ反映するだけです。
 */
UCLASS(Blueprintable)
class REBOARGETCH_API UFacilitySignWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFacilitySignWidget(const FObjectInitializer& ObjectInitializer);

    /** フォントのSizeはpt、GapPxは2行間の余白（px）。 */
    void SetContent(const FText& Title, const FText& Subtitle, const FSlateFontInfo& TitleFont, const FSlateFontInfo& SubtitleFont,
                    const FLinearColor& TitleColor, const FLinearColor& SubtitleColor, float GapPx);

    /** 階層の見た目。TierStylesに無い階層は構造体の既定値を返します。 */
    FFacilitySignStyle GetStyle(EFacilitySignTier Tier) const;
    /** Designerで設定した書体・字間を保ったままSizeだけ差し替えます。Title/SubtitleText未バインド時はEngine既定フォントを返します。 */
    FSlateFontInfo MakeTitleFont(int32 Points) const;
    FSlateFontInfo MakeSubtitleFont(int32 Points) const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style") TMap<EFacilitySignTier, FFacilitySignStyle> TierStyles;
    /** 空き枠など、暗く表示するときの明るさ倍率。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style", meta=(ClampMin="0", ClampMax="1")) float DimmedBrightness=.35f;
    /** タイトル1文字の描画解像度（px）。階層に関わらずこの高さで描いて縮小配置します。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style", meta=(ClampMin="8")) float TitlePixelHeight=64.f;
    /** タイトルとサブタイトルの行間（Paddingに対する割合）。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style", meta=(ClampMin="0")) float SubtitleGapRatio=.5f;
    /** 下端のアクセントラインの高さ＝TitleSize×Ratio（最小MinHeight）。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style|Accent Line", meta=(ClampMin="0")) float AccentLineHeightRatio=.06f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style|Accent Line", meta=(Units="cm", ClampMin="0")) float AccentLineMinHeight=1.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style|Accent Line", meta=(Units="cm", ClampMin="0.01")) float AccentLineDepth=.6f;
    /** パネル前面からの手前方向オフセット。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style|Accent Line", meta=(Units="cm")) float AccentLineOffset=.3f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Sign Style", meta=(Units="cm")) float TextSurfaceOffset=.5f;

protected:
    UPROPERTY(BlueprintReadOnly, Category="Sign", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(BlueprintReadOnly, Category="Sign", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SubtitleText;
};
