#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarHUDWidget.generated.h"

class UTextBlock;
class UImage;
class UWidget;
class AGadgetBase;

/** ゲームプレイ中にHPや捕獲数を常時表示するHUDの基底Widgetです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 現在の捕獲数とステージ目標数を表示へ反映します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|HUD")
	void UpdateCaptureCount(int32 CurrentCount, int32 TargetCount);

	/** 現在HPと最大HPを表示へ反映します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|HUD")
	void UpdateHealth(float CurrentHealth, float MaxHealth);

	/** 4つの装備内容と現在選択中の枠を表示へ反映します。 */
	void UpdateGadgetSlots(const TArray<TSubclassOf<AGadgetBase>>& GadgetSlots, int32 SelectedSlotIndex);

protected:
	virtual void NativeConstruct() override;

	/** WBP内で現在捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName CapturedCountTextWidgetName;

	/** WBP内で目標捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName TargetCountTextWidgetName;

	/** WBP内で現在HPを表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName CurrentHealthTextWidgetName;

	/** WBP内で最大HPを表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName MaxHealthTextWidgetName;

	/** Slot 1～4のガジェット画像用Image名です。要素順がスロット番号に対応します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	TArray<FName> GadgetIconWidgetNames;

	/** Slot 1～4の選択アウトライン用Widget名です。ImageやBorderなど任意のWidgetを指定できます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	TArray<FName> GadgetSelectionWidgetNames;

private:
	void ResolveWidgetReferences();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CapturedCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TargetCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CurrentHealthText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MaxHealthText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> GadgetIconWidgets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> GadgetSelectionWidgets;
};
