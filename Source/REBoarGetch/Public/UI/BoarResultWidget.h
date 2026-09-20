#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Stage/StageRunData.h"
#include "BoarResultWidget.generated.h"

class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnResultDismissRequested);

/** ステージ終了時のプレイ結果を表示する基底Widgetです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** リザルトへ表示する捕獲数を設定します。 */
	void InitializeResult(int32 CapturedCount, int32 TargetCount);
	/** 保存前に確定した今回の結果だけを受け取ります。 */
	void InitializeRunResult(const FStageRunData& Run);
	UPROPERTY(BlueprintReadOnly, Category = "REBoarGetch|Result") FStageRunData ResultData;
	/** NEW個体一覧などをReusable Widgetへ展開するための通知です。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Result") void OnRunResultUpdated();

	/** 任意入力を受け取れるようWidget自身へフォーカスを設定します。 */
	void FocusForDismissInput();

	/** リザルトを閉じてロビーへ戻る入力をPlayerControllerへ通知します。 */
	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Result")
	FOnResultDismissRequested OnDismissRequested;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName ClearTimeTextWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName NewBoarCountTextWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName SpecialCoinCountTextWidgetName;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** WBP内で現在の捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName CapturedCountTextWidgetName;

	/** WBP内で目標捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName TargetCountTextWidgetName;

private:
	void ResolveWidgetReferences();
	FReply RequestDismiss();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CapturedCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TargetCountText;

};
