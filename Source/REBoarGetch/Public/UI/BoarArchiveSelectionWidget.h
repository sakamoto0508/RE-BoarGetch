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
	UBoarArchiveSelectionWidget(const FObjectInitializer& ObjectInitializer);
    UPROPERTY(BlueprintAssignable) FOnArchiveSelectionClosed OnClosed;
    UPROPERTY(EditDefaultsOnly,Category="Archive") TArray<TObjectPtr<class UStageConfig>> StageCatalog;
    UPROPERTY(EditDefaultsOnly,Category="Archive") TSubclassOf<UBoarLoadoutEntry> EntryClass;
    void FocusInitialChoice();
	bool IsReadyForDisplay() const { return BackButton && SlotsPanel && Grid && Status; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    UFUNCTION() void SelectSlot(int32 Index);
    UFUNCTION() void ChooseBoar(int32 Index);
	UFUNCTION() void RemoveBoar();
    UFUNCTION() void Confirm();
    UFUNCTION() void Cancel();
    void RefreshSlotRows();
	void RefreshDetail();
	void MoveGridFocus(int32 Offset);
	void AssignBoar(FName Id);
	void UpdatePortrait(UBoarLoadoutEntry* Card,FName Id,const FText& Badge,bool bActive);
    FText LabelFor(FName Id) const;
    TArray<FName> PendingIds;
    TArray<FName> Candidates;
    int32 ActiveSlot=0;
	int32 FocusedCandidate=INDEX_NONE;
	bool bInitialFocusPending=false;
	double NextAnalogNavigation=0;
    UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> SlotRows;
    UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> CandidateRows;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<class UHorizontalBox> SlotsPanel;
	UPROPERTY(Transient) TObjectPtr<class UUniformGridPanel> Grid;
	UPROPERTY(Transient) TObjectPtr<class UScrollBox> GridScroll;
	UPROPERTY(Transient) TObjectPtr<class UButton> BackButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> SetButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;
	UPROPERTY(Transient) TObjectPtr<class UImage> DetailPhoto;
};
