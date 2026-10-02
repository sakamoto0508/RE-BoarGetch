#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarLoadoutWidget.generated.h"

class UBoarLoadoutEntry;
class UBoarLoadoutPresenter;
struct FBoarLoadoutEntryViewData;
class UPanelWidget;
class UTextBlock;
class UImage;
class UButton;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadoutClosed);

/** Catalog順の解放済み候補を表示し、既存GadgetComponentへ変更を依頼します。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarLoadoutWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable) FOnLoadoutClosed OnClosed;
	void FocusInitialChoice();
	/** Set before AddToViewport. Pause defaults to equipment-only. */
	void SetLabContext(bool bEnabled) { bRequestedLabContext = bEnabled; }
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	UPROPERTY(EditDefaultsOnly, Category = "Loadout") TSubclassOf<UBoarLoadoutEntry> EntryClass;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName SlotsPanelName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName CandidatesPanelName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName DetailNameWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName DetailRoleWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName DetailDescriptionWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName DetailIconWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName StatusWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName BackButtonName;
private:
	UFUNCTION() void RebuildCandidates();
	UFUNCTION() void RefreshSlots();
	UFUNCTION() void ChooseSlot(int32 Index);
	UFUNCTION() void ChooseCandidate(int32 Index);
	UFUNCTION() void ChooseLabMode(int32 Index);
	UFUNCTION() void PreviewSlot(int32 Index);
	UFUNCTION() void PreviewCandidate(int32 Index);
	UFUNCTION() void Close();
	UFUNCTION() void FocusBack();
	void ShowDetails(const FBoarLoadoutEntryViewData& Data);
	void SetStatus(const FText& Text, bool bError = false);
	UPROPERTY(Transient) TObjectPtr<UBoarLoadoutPresenter> Presenter;
	UPROPERTY(Transient) TObjectPtr<UPanelWidget> SlotsPanel;
	UPROPERTY(Transient) TObjectPtr<UPanelWidget> CandidatesPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailRole;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailDescription;
	UPROPERTY(Transient) TObjectPtr<UImage> DetailIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> SlotEntries;
	UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> CandidateEntries;
	bool bRequestedLabContext = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> LabModeEntries;
	bool bInitialFocusPending = true;
};
