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

/** 装備スロット・候補・詳細を描画するView。操作の判断とモデルへの変更要求はPresenterへ渡します。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarLoadoutWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** ガジェット編成画面を閉じたことを親画面へ通知する。 */
	UPROPERTY(BlueprintAssignable)
	FOnLoadoutClosed OnClosed;

	void FocusInitialChoice();
	/** AddToViewport前に指定します。Pauseから開く場合は装備変更だけを使用します。 */
	void SetLabContext(bool bEnabled) { bRequestedLabContext = bEnabled; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	/** 装備枠と候補に使う項目Widgetクラス。 */
	UPROPERTY(EditDefaultsOnly, Category = "Loadout")
	TSubclassOf<UBoarLoadoutEntry> EntryClass;

	/** WBPで装備枠のコンテナを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName SlotsPanelName;

	/** WBPで候補一覧のコンテナを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName CandidatesPanelName;

	/** WBPでガジェット名欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName DetailNameWidgetName;

	/** WBPでガジェットの役割欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName DetailRoleWidgetName;

	/** WBPでガジェットの説明欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName DetailDescriptionWidgetName;

	/** WBPでガジェットのアイコンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName DetailIconWidgetName;

	/** WBPで操作結果欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName StatusWidgetName;

	/** WBPで戻るボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName BackButtonName;

private:
	UFUNCTION()
	void RebuildCandidates();

	UFUNCTION()
	void RefreshSlots();

	UFUNCTION()
	void ChooseSlot(int32 Index);

	UFUNCTION()
	void ChooseCandidate(int32 Index);

	UFUNCTION()
	void ChooseLabMode(int32 Index);

	UFUNCTION()
	void PreviewSlot(int32 Index);

	UFUNCTION()
	void PreviewCandidate(int32 Index);

	UFUNCTION()
	void Close();

	UFUNCTION()
	void FocusBack();

	void ShowDetails(const FBoarLoadoutEntryViewData& Data);
	void SetStatus(const FText& Text, bool bError = false);

	/** 装備枠・候補・解放操作の状態を管理するPresenter。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarLoadoutPresenter> Presenter;

	/** 現在の装備枠を並べるコンテナ。 */
	UPROPERTY(Transient)
	TObjectPtr<UPanelWidget> SlotsPanel;

	/** 選べるガジェットを並べるコンテナ。 */
	UPROPERTY(Transient)
	TObjectPtr<UPanelWidget> CandidatesPanel;

	/** フォーカスしたガジェット名の表示欄。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailName;

	/** フォーカスしたガジェットの役割の表示欄。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailRole;

	/** フォーカスしたガジェットの説明の表示欄。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailDescription;

	/** フォーカスしたガジェットのアイコン。 */
	UPROPERTY(Transient)
	TObjectPtr<UImage> DetailIcon;

	/** 装備・解放操作の結果を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Status;

	/** 編成画面を閉じるボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> BackButton;

	/** 選択と表示更新に使う装備枠の項目一覧。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoarLoadoutEntry>> SlotEntries;

	/** 選択と表示更新に使う候補の項目一覧。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoarLoadoutEntry>> CandidateEntries;

	/** 画面構築時に研究所用の操作を表示する要求があるか。 */
	bool bRequestedLabContext = false;

	/** 研究所の装備・解放・試用を切り替える項目一覧。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoarLoadoutEntry>> LabModeEntries;

	/** 画面を開いた後に初期フォーカスを設定する必要があるか。 */
	bool bInitialFocusPending = true;
};
