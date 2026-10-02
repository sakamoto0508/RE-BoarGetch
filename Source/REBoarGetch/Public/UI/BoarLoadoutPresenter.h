#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BoarLoadoutPresenter.generated.h"

class UGadgetComponent;
class UBoarGadgetSubsystem;
class AGadgetBase;
class APlayerController;
class UTexture2D;

/** View receives display data, never Gadget definitions or SaveData. */
struct FBoarLoadoutEntryViewData
{
	FText Label;
	FText Name;
	FText Role;
	FText Description;
	UTexture2D *Icon = nullptr; // Borrowed from the model's referenced definition.
	bool bSelected = false;
};

struct FBoarLoadoutActionResult
{
	FText Status;
	bool bError = false;
	bool bRefreshCandidates = false;
	bool bFocusSlots = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadoutViewChanged);

/** Per-open-menu presentation state and intents. No ownership of persistent progress. */
UCLASS()
class REBOARGETCH_API UBoarLoadoutPresenter : public UObject
{
	GENERATED_BODY()
  public:
	bool Initialize(APlayerController *Owner, bool bRequestedLabContext);
	void Shutdown();
	UPROPERTY() FOnLoadoutViewChanged OnSlotsChanged;
	UPROPERTY() FOnLoadoutViewChanged OnCandidatesChanged;
	bool IsLabContext() const
	{
		return bLabContext;
	}
	int32 GetSelectedSlot() const
	{
		return SelectedSlot;
	}
	int32 GetLabMode() const
	{
		return LabMode;
	}
	int32 GetSlotCount() const;
	int32 GetCandidateCount() const
	{
		return Candidates.Num();
	}
	void RebuildCatalog();
	FBoarLoadoutEntryViewData GetSlot(int32 Index) const;
	FBoarLoadoutEntryViewData GetCandidate(int32 Index) const;
	int32 ChooseSlot(int32 Index);
	bool ChooseLabMode(int32 Index);
	FText GetLabModeStatus() const;
	FBoarLoadoutActionResult ChooseCandidate(int32 Index);

  private:
	FBoarLoadoutEntryViewData BuildDetails(TSubclassOf<AGadgetBase> Class) const;
	UFUNCTION() void HandleSlotsChanged();
	UFUNCTION() void HandleCandidatesChanged();
	UPROPERTY(Transient) TObjectPtr<UGadgetComponent> Gadgets;
	UPROPERTY(Transient) TObjectPtr<UBoarGadgetSubsystem> Progress;
	UPROPERTY(Transient) TArray<TSubclassOf<AGadgetBase>> Candidates;
	int32 SelectedSlot = 0;
	int32 LabMode = 0;
	bool bLabContext = false;
};
