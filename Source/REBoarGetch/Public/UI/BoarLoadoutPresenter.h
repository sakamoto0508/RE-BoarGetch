#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BoarLoadoutPresenter.generated.h"

class UGadgetComponent;
class UBoarGadgetSubsystem;
class AGadgetBase;
class APlayerController;
class UTexture2D;

/** Viewへ渡す表示データ。装備定義やSaveDataの参照・判断はPresenter内で行います。 */
struct FBoarLoadoutEntryViewData
{
	FText Label;
	FText Name;
	FText Role;
	FText Description;
	UTexture2D *Icon = nullptr; // モデルが参照する定義から借用するアイコンです。
	bool bSelected = false;
};

/** 操作後の状態文と、候補再描画・スロットへのフォーカス復帰の指示です。 */
struct FBoarLoadoutActionResult
{
	FText Status;
	bool bError = false;
	bool bRefreshCandidates = false;
	bool bFocusSlots = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadoutViewChanged);

/** 画面を開いている間の選択状態と操作要求を担当します。永続進捗はSubsystemが管理します。 */
UCLASS()
class REBOARGETCH_API UBoarLoadoutPresenter : public UObject
{
	GENERATED_BODY()
  public:
	/** モデルの取得に失敗した場合はfalseを返し、Viewに利用不能状態を表示させます。 */
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
	// 0: 装備変更、1: 研究所での解放、2: 装備枠とは別のTest対象選択。
	int32 LabMode = 0;
	bool bLabContext = false;
};
