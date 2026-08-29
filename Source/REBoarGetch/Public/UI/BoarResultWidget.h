#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarResultWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnResultRetryRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnResultTitleRequested);

/** ステージ終了時のプレイ結果を表示する基底Widgetです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** リザルトへ表示する捕獲数を設定します。 */
	void InitializeResult(int32 CapturedCount, int32 TargetCount);

	/** リトライボタンへ最初のUIフォーカスを設定します。 */
	void FocusInitialControl();

	/** リトライが要求されたことをPlayerControllerへ通知します。 */
	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Result")
	FOnResultRetryRequested OnRetryRequested;

	/** タイトル遷移が要求されたことをPlayerControllerへ通知します。 */
	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Result")
	FOnResultTitleRequested OnTitleRequested;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** WBP内で現在の捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName CapturedCountTextWidgetName;

	/** WBP内で目標捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName TargetCountTextWidgetName;

	/** WBP内でリトライに使用するButtonの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName RetryButtonWidgetName;

	/** WBP内でタイトル遷移に使用するButtonの名前を指定します。未設定でも構いません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName TitleButtonWidgetName;

private:
	void ResolveWidgetReferences();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CapturedCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TargetCountText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RetryButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> TitleButton;

	UFUNCTION()
	void HandleRetryClicked();

	UFUNCTION()
	void HandleTitleClicked();
};
