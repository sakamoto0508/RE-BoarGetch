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
	/** 選択枠の発光に使うマテリアル。 */
	UPROPERTY(EditDefaultsOnly, Category="Visual")
 TObjectPtr<class UMaterialInterface> FlowGlowMaterial;
 /** 編集中・フォーカス中の枠色です。 */
 UPROPERTY(EditDefaultsOnly, Category="Visual")
 FLinearColor ActiveColor=FLinearColor(1,0.66f,0.08f);

 /** 通常時の枠色です。 */
 UPROPERTY(EditDefaultsOnly, Category="Visual")
 FLinearColor IdleColor=FLinearColor(0.06f,0.54f,0.75f);

 /** Flow Glow枠を合わせるカードの大きさです。WBPのレイアウトと揃えます。 */
 UPROPERTY(EditDefaultsOnly, Category="Visual")
 FVector2D CardSize=FVector2D(206,242);

 void SetPortrait(class UTexture2D* Texture, const FText& Caption, const FText& Badge, bool bActive);

protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeOnAddedToFocusPath(const FFocusEvent& Event) override;
 virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& Event) override;
 // レイアウトはWBPで作成します。C++で代替レイアウトは作らず、RootWidgetが無い場合はErrorを出して表示しません。
 /** カード全体の背景と外枠を表示するBorder。 */
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional))
 TObjectPtr<class UBorder> Frame;

 /** 個体の写真を表示する画像。 */
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional))
 TObjectPtr<class UImage> Portrait;

 /** フォーカス中の強調枠を表示する画像。 */
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional))
 TObjectPtr<class UImage> FocusFrame;

 /** 写真を表示できないときのプレースホルダー。 */
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional))
 TObjectPtr<class UTextBlock> Placeholder;

 /** 展示枠や個体に付くバッジを表示するテキスト。 */
 UPROPERTY(BlueprintReadOnly, Category="Bindings", meta=(BindWidgetOptional))
 TObjectPtr<class UTextBlock> BadgeText;

private:
 void RefreshFrame();
 /** 編集中の展示枠か（bActiveCard）、フォーカス中か（bFocusedCard）。 */
 bool bActiveCard=false, bFocusedCard=false;
};
