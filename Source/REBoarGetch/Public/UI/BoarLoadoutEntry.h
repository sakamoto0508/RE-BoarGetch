#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarLoadoutEntry.generated.h"

class UButton;
class UTextBlock;
class UImage;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadoutEntry, int32, Index);

/** スロット／候補の共通行。見た目はBlueprintで設定します。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarLoadoutEntry : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(int32 InIndex, const FText& Text, bool bSelected = false);
	void FocusEntry();
	void SetAvailable(bool bAvailable);
	void UseCompactLabRow();
	// 決定とフォーカスを別々に通知し、親画面で変更とプレビューを分けられるようにします。
	/** この項目が決定されたことを親画面へ通知する。 */
	FOnLoadoutEntry OnChosen;
	/** プレビュー対象が変わったことを親画面へ通知する。 */
	FOnLoadoutEntry OnFocused;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& Event) override;

	/** WBPで項目の決定ボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName ButtonName;

	/** WBPで項目ラベルを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName LabelName;

	/** WBPで選択表示を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName SelectionImageName;

	/** WBPでロック表示を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName LockedIndicatorWidgetName;

private:
	UFUNCTION()
	void Choose();

	UFUNCTION()
	void Hover();

	void Refresh();
	/** 親画面が装備枠または候補と対応付ける項目の添字。 */
	int32 EntryIndex = INDEX_NONE;
	/** 項目に表示するガジェットや装備枠の文言。 */
	FText EntryText;
	/** 現在選択されている項目か。 */
	bool bEntrySelected = false;
	/** 装備・解放操作の候補として有効か。 */
	bool bEntryAvailable = true;

	/** 未解放の項目を示すWidget。 */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> LockedIndicator;

	/** 項目を選択・決定するボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> ActionButton;

	/** 項目の名称を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Label;

	/** 親画面の選択状態を示す画像。 */
	UPROPERTY(Transient)
	TObjectPtr<UImage> SelectionImage;
};
