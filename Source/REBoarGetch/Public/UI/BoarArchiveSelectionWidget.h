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
    /** 旧設定です。GameInstanceのStageCatalogが未設定の間だけ使います。設定後は空にしてください。 */
    UPROPERTY(EditDefaultsOnly,Category="Archive") TArray<TObjectPtr<class UStageConfig>> StageCatalog;
    UPROPERTY(EditDefaultsOnly,Category="Archive") TSubclassOf<UBoarLoadoutEntry> EntryClass;
    /** 通常メッセージの文字色です。 */
    UPROPERTY(EditDefaultsOnly,Category="Archive|Style") FLinearColor StatusColor=FLinearColor(0.12f,0.74f,0.93f);
    /** 保存失敗時の文字色です。 */
    UPROPERTY(EditDefaultsOnly,Category="Archive|Style") FLinearColor ErrorColor=FLinearColor(1,0.24f,0.2f);
    void FocusInitialChoice();
	bool IsReadyForDisplay() const { return BackButton && SlotsPanel && Grid && Status; }
protected:
	// レイアウトはWBPで作成します。WBPにRootWidgetが無い場合だけ、C++の仮レイアウトを構築します。
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Status;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<class UHorizontalBox> SlotsPanel;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<class UUniformGridPanel> Grid;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<class UScrollBox> GridScroll;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<class UButton> BackButton;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<class UButton> SetButton;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<class UButton> RemoveButton;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DetailText;
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional)) TObjectPtr<class UImage> DetailPhoto;
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
	void BuildFallbackLayout();
	void SetStatus(const FText& Message,bool bError=false);
    FText LabelFor(FName Id) const;
    // 未保存の一括編集ではなく、保存済み展示枠を表示するためのコピーです。変更成功時に読み直します。
    TArray<FName> PendingIds;
    TArray<FName> Candidates;
    int32 ActiveSlot=0;
	int32 FocusedCandidate=INDEX_NONE;
	bool bInitialFocusPending=false;
	// WorldのPause状態に依存しない、実時間によるスティック連続移動の待機期限です。
	double NextAnalogNavigation=0;
    UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> SlotRows;
    UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> CandidateRows;
};
