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
	/** 装備枠または候補の一覧に表示する文言。 */
	FText Label;
	/** 詳細欄に表示するガジェット名。 */
	FText Name;
	/** 詳細欄に表示するガジェットの役割。 */
	FText Role;
	/** 詳細欄に表示するガジェットの説明。 */
	FText Description;
	/** ガジェット定義が所有する詳細表示用の借用アイコン。 */
	UTexture2D *Icon = nullptr;
	/** 現在の装備枠として選択中か。 */
	bool bSelected = false;
};

/** 操作後の状態文と、候補再描画・スロットへのフォーカス復帰の指示です。 */
struct FBoarLoadoutActionResult
{
	/** 編成操作の結果として画面に表示する文言。 */
	FText Status;
	/** 結果文言をエラーとして表示するか。 */
	bool bError = false;
	/** 編成操作の後に候補一覧を再取得する必要があるか。 */
	bool bRefreshCandidates = false;
	/** 編成操作の後に装備枠へフォーカスを戻すか。 */
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

	/** 装備枠の表示データが変わったことを画面へ通知する。 */
	UPROPERTY()
	FOnLoadoutViewChanged OnSlotsChanged;

	/** 候補一覧の表示データが変わったことを画面へ通知する。 */
	UPROPERTY()
	FOnLoadoutViewChanged OnCandidatesChanged;

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

	UFUNCTION()
	void HandleSlotsChanged();

	UFUNCTION()
	void HandleCandidatesChanged();

	/** 編成対象のガジェット一覧を取得するサブシステム。 */
	UPROPERTY(Transient)
	TObjectPtr<UGadgetComponent> Gadgets;

	/** 解放・保存済み装備を取得する永続進行の参照。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarGadgetSubsystem> Progress;

	/** 現在のモードで画面に提示する候補ガジェットの一覧。 */
	UPROPERTY(Transient)
	TArray<TSubclassOf<AGadgetBase>> Candidates;

	/** 現在編集している装備枠の添字。 */
	int32 SelectedSlot = 0;
	/** 研究所での操作モード。0は装備変更、1は解放、2は試用対象選択。 */
	int32 LabMode = 0;
	/** 研究所の解放・試用操作を表示できる画面か。 */
	bool bLabContext = false;
};
