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
	UPROPERTY(EditDefaultsOnly, Category="Visual") TObjectPtr<class UMaterialInterface> FlowGlowMaterial;
 void SetPortrait(class UTexture2D* Texture, const FText& Caption, const FText& Badge, bool bActive);
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeOnAddedToFocusPath(const FFocusEvent& Event) override;
 virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& Event) override;
private:
 void RefreshFrame();
 bool bActiveCard=false, bFocusedCard=false;
 UPROPERTY(Transient) TObjectPtr<class UBorder> Frame;
 UPROPERTY(Transient) TObjectPtr<class UImage> Portrait;
	UPROPERTY(Transient) TObjectPtr<class UImage> GlowFrame;
 UPROPERTY(Transient) TObjectPtr<class UTextBlock> Placeholder;
 UPROPERTY(Transient) TObjectPtr<class UTextBlock> BadgeText;
};
