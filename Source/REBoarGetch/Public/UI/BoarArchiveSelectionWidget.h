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

    /** 展示枠選択画面を閉じたことを外側へ通知する。 */
    UPROPERTY(BlueprintAssignable)
    FOnArchiveSelectionClosed OnClosed;

    /** 旧設定です。GameInstanceのStageCatalogが未設定の間だけ使います。設定後は空にしてください。 */
    UPROPERTY(EditDefaultsOnly,Category="Archive")
    TArray<TObjectPtr<class UStageConfig>> StageCatalog;

    /** 展示枠と候補個体のカードに使うWidgetクラス。 */
    UPROPERTY(EditDefaultsOnly,Category="Archive")
    TSubclassOf<UBoarLoadoutEntry> EntryClass;

    /** 通常メッセージの文字色です。 */
    UPROPERTY(EditDefaultsOnly,Category="Archive|Style")
    FLinearColor StatusColor=FLinearColor(0.12f,0.74f,0.93f);

    /** 保存失敗時の文字色です。 */
    UPROPERTY(EditDefaultsOnly,Category="Archive|Style")
    FLinearColor ErrorColor=FLinearColor(1,0.24f,0.2f);

    void FocusInitialChoice();
	bool IsReadyForDisplay() const { return BackButton && SlotsPanel && Grid && Status; }

protected:
	// レイアウトはWBPで作成します。C++で代替レイアウトは作らず、RootWidgetが無い場合はErrorを出して表示しません。
	/** 保存結果や操作案内を表示するテキスト。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> Status;

	/** 編集対象となる展示枠カードのコンテナ。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<class UHorizontalBox> SlotsPanel;

	/** 登録済み個体の候補カードを並べるグリッド。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<class UUniformGridPanel> Grid;

	/** 個体の候補一覧をスクロールする領域。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<class UScrollBox> GridScroll;

	/** 展示枠の編集画面を閉じるボタン。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<class UButton> BackButton;

	/** 選択中の候補を現在の展示枠へ割り当てるボタン。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<class UButton> SetButton;

	/** 現在の展示枠を空にするボタン。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<class UButton> RemoveButton;

	/** 選択中の候補の詳細を表示するテキスト。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailText;

	/** 選択中の候補の詳細写真を表示する画像。 */
	UPROPERTY(BlueprintReadOnly,Category="Archive|Bindings",meta=(BindWidgetOptional))
	TObjectPtr<class UImage> DetailPhoto;

	virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;

private:
    UFUNCTION()
    void SelectSlot(int32 Index);

    UFUNCTION()
    void ChooseBoar(int32 Index);

	UFUNCTION()
	void RemoveBoar();

    UFUNCTION()
    void Confirm();

    UFUNCTION()
    void Cancel();

    void RefreshSlotRows();
	void RefreshDetail();
	void MoveGridFocus(int32 Offset);
	void AssignBoar(FName Id);
	void UpdatePortrait(UBoarLoadoutEntry* Card,FName Id,const FText& Badge,bool bActive);
	void SetStatus(const FText& Message,bool bError=false);
    FText LabelFor(FName Id) const;
    /** 表示用に保持する保存済み展示枠IDのコピー。変更成功時に読み直す。 */
    TArray<FName> PendingIds;
    /** 展示枠に設定できる登録済み個体の一覧。 */
    TArray<FName> Candidates;
    /** 編集中の展示枠の添字。 */
    int32 ActiveSlot=0;
	/** フォーカス中の候補個体の添字。 */
	int32 FocusedCandidate=INDEX_NONE;
	/** 表示後に初期フォーカスの設定が必要か。 */
	bool bInitialFocusPending=false;
	/** スティック連続移動を次に受け付ける実時刻（秒）。 */
	double NextAnalogNavigation=0;

    /** 展示枠の表示とフォーカス更新に使うカード一覧。 */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBoarLoadoutEntry>> SlotRows;

    /** 候補個体の表示とフォーカス更新に使うカード一覧。 */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBoarLoadoutEntry>> CandidateRows;
};
