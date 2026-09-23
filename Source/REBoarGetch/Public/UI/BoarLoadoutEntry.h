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
	FOnLoadoutEntry OnChosen;
	FOnLoadoutEntry OnFocused;
protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& Event) override;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName ButtonName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName LabelName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName SelectionImageName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName LockedIndicatorWidgetName;
private:
	UFUNCTION() void Choose();
	UFUNCTION() void Hover();
	void Refresh();
	int32 EntryIndex = INDEX_NONE;
	FText EntryText;
	bool bEntrySelected = false;
	bool bEntryAvailable = true;
	UPROPERTY(Transient) TObjectPtr<UWidget> LockedIndicator;
	UPROPERTY(Transient) TObjectPtr<UButton> ActionButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
	UPROPERTY(Transient) TObjectPtr<UImage> SelectionImage;
};
