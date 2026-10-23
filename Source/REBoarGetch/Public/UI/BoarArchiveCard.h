#pragma once
#include "CoreMinimal.h"
#include "UI/BoarLoadoutEntry.h"
#include "BoarArchiveCard.generated.h"
/** 既存LoadoutEntryのFocus/選択イベントを使う、展示専用Portrait Card。 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarArchiveCard : public UBoarLoadoutEntry
{
 GENERATED_BODY()
public:
 UBoarArchiveCard(const FObjectInitializer& ObjectInitializer);
	UPROPERTY(EditDefaultsOnly, Category="Visual")
 TObjectPtr<class UMaterialInterface> FlowGlowMaterial;
 /** 編集中・フォーカス中の枠色です。 */
 UPROPERTY(EditDefaultsOnly, Category="Visual") FLinearColor ActiveColor=FLinearColor(1,0.66f,0.08f);
 /** 通常時の枠色です。 */
 UPROPERTY(EditDefaultsOnly, Category="Visual") FLinearColor IdleColor=FLinearColor(0.06f,0.54f,0.75f);
 /** Flow Glow枠を合わせるカードの大きさです。WBPのレイアウトと揃えます。 */
 UPROPERTY(EditDefaultsOnly, Category="Visual") FVector2D CardSize=FVector2D(206,242);
 void SetPortrait(class UTexture2D* Texture, const FText& Caption, const FText& Badge, bool bActive);
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeOnAddedToFocusPath(const FFocusEvent& Event) override;
 virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& Event) override;
 // レイアウトはWBPで作成します。WBPにRootWidgetが無い場合だけ、C++の仮レイアウトを構築します。
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional)) TObjectPtr<class UBorder> Frame;
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional)) TObjectPtr<class UImage> Portrait;
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional)) TObjectPtr<class UImage> FocusFrame;
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional)) TObjectPtr<class UTextBlock> Placeholder;
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional)) TObjectPtr<class UTextBlock> BadgeText;
private:
 void BuildFallbackLayout();
 void RefreshFrame();
 // 編集対象の展示枠と現在のフォーカスを別々に保持して枠の強調を決めます。
 bool bActiveCard=false, bFocusedCard=false;
};
