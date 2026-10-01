#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarArchiveSelectionWidget.generated.h"
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArchiveSelectionClosed);
class UBoarLoadoutEntry;
class UTextBlock;
/** 3枠の編集候補だけを保持。保存の正本はGameInstance / SaveGameです。 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarArchiveSelectionWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable) FOnArchiveSelectionClosed OnClosed;
    UPROPERTY(EditDefaultsOnly,Category="Archive") TArray<TObjectPtr<class UStageConfig>> StageCatalog;
    UPROPERTY(EditDefaultsOnly,Category="Archive") TSubclassOf<UBoarLoadoutEntry> EntryClass;
    void FocusInitialChoice();
protected:
    virtual void NativeConstruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    UFUNCTION() void SelectSlot(int32 Index);
    UFUNCTION() void ChooseBoar(int32 Index);
    UFUNCTION() void Confirm();
    UFUNCTION() void Cancel();
    void RefreshSlotRows();
    FText LabelFor(FName Id) const;
    TArray<FName> PendingIds;
    TArray<FName> Candidates;
    int32 ActiveSlot=0;
    UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> SlotRows;
    UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> CandidateRows;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
};
