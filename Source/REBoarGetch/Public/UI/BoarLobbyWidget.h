#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "BoarLobbyWidget.generated.h"

class UBoarStageSelectPresenter;
struct FBoarStageSelectViewData;
class UButton;
class UImage;
class UStageConfig;
class UTextBlock;
class UBoarLoadoutEntry;
class UScrollBox;
class ABoarStagePreviewActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyStageStartRequested, UStageConfig*, StageConfig);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLobbyStageSelectionClosed);

/** Stage Selectの描画、Focus、入力、Carousel/Preview表現を担当するViewです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarLobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 指定ステージの表示内容を更新し、決定操作を受け付けます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Lobby")
	void ShowStageSelection(UStageConfig* StageConfig);
	/** カタログをPresenterへ渡し、一覧またはカルーセルの表示を組み立てます。 */
	void ShowStageCatalog(const TArray<UStageConfig*>& Stages, UStageConfig* FallbackStage);
	void ShowTravelError(const FText& Message);

	/** ステージ選択表示を閉じます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Lobby")
	void HideStageSelection();

	/** 開始要求をStageEntranceへ通知します。View自身は進捗保存やレベル遷移を行いません。 */
	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Lobby")
	FOnLobbyStageStartRequested OnStageStartRequested;

	/** ステージ選択画面を閉じたことを呼び出し元へ通知する。 */
	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Lobby")
	FOnLobbyStageSelectionClosed OnStageSelectionClosed;

protected:
	/** WBPで図鑑ボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Lobby Widget References")
	FName EncyclopediaButtonName;

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	/** ステージ一覧に使う項目Widgetクラス。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select")
	TSubclassOf<UBoarLoadoutEntry> StageEntryClass;

	/** ステージ候補をカルーセル形式で表示するか。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select")
	bool bUseCarousel = false;

	/** ミニチュアの描画結果をカードへ反映するマテリアル。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Preview")
	TObjectPtr<UMaterialInterface> PreviewMaterial;

	/** ステージカードを横に送り切るまでの時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select", meta=(ClampMin="0.05", ClampMax="0.5"))
	float CarouselDuration = .18f;

	/** 前のステージへ移動する入力キーの一覧。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select")
	TArray<FKey> PreviousStageKeys = { EKeys::Left, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftShoulder };

	/** 次のステージへ移動する入力キーの一覧。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select")
	TArray<FKey> NextStageKeys = { EKeys::Right, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_RightShoulder };

	/** ステージ選択を閉じる入力キーの一覧。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select")
	TArray<FKey> CancelKeys = { EKeys::Escape, EKeys::Gamepad_FaceButton_Right };

	/** 決定キー（フォーカス中のボタンを実行。戻るボタン以外なら開始）。空ならSlate標準の決定（Enter・パッド下ボタン）だけを使います。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select")
	TArray<FKey> ConfirmKeys;

	/** カルーセルの隣カードまでの距離（px）と、隣カードの不透明度（前／次）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select", meta = (ClampMin = "0"))
	float CarouselSpacing = 470.f;

	/** 前のステージカードの通常不透明度（0～1）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select", meta = (ClampMin = "0", ClampMax = "1"))
	float PreviousCardOpacity = .58f;

	/** 次のステージカードの通常不透明度（0～1）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select", meta = (ClampMin = "0", ClampMax = "1"))
	float NextCardOpacity = .38f;

	/** 特別コイン・図鑑登録の「取得数 / 総数」を表示するTextBlockと、クリア済みのときだけ出すCLEAR表示。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName CoinProgressTextWidgetName;

	/** WBPで図鑑進捗欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName EncyclopediaProgressTextWidgetName;

	/** WBPでクリア済みバッジを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName ClearBadgeWidgetName;

	/** 開始／戻るボタン背面の枠Image。フォーカス中の方を選択テクスチャにします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName StartButtonFrameImageName;

	/** WBPでキャンセルボタンの枠画像を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName CancelButtonFrameImageName;

	/** 選択中の操作ボタンに表示する画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	TObjectPtr<class UTexture2D> ButtonSelectedTexture;

	/** 通常時の操作ボタンに表示する画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	TObjectPtr<class UTexture2D> ButtonNormalTexture;

	/** WBPでステージ一覧を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName StageListWidgetName;

	/** WBPで選択ステージの状態欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName StageStatusWidgetName;

	/** WBPで選択ステージの進捗欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName StageProgressWidgetName;

	/** WBPで前へ送るボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName PreviousButtonName;

	/** WBPで次へ送るボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings")
	FName NextButtonName;

	/** ステージ名を表示する任意名のTextBlockです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Lobby Widget References")
	FName StageNameTextWidgetName;

	/** ステージ説明を表示する任意名のTextBlockです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Lobby Widget References")
	FName StageDescriptionTextWidgetName;

	/** 捕獲目標数を表示する任意名のTextBlockです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Lobby Widget References")
	FName TargetCaptureCountTextWidgetName;

	/** ステージサムネイルを表示する任意名のImageです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Lobby Widget References")
	FName StageThumbnailImageWidgetName;

	/** ステージ開始用の任意名Buttonです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Lobby Widget References")
	FName StartStageButtonWidgetName;

	/** 選択を閉じる任意名Buttonです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Lobby Widget References")
	FName CancelButtonWidgetName;

private:
	UMaterialInstanceDynamic* GetDioramaBrush(const FBoarStageSelectViewData& Stage);
	void ReleasePreviews();
	/** 表示範囲内のミニチュアActorをStageIdで保持する。 */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<ABoarStagePreviewActor>> PreviewActors;

	/** カード描画用のBrushをStageIdで保持する。 */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> PreviewBrushes;

	UFUNCTION()
	void RefreshSelectedStage();

	void FocusSelection();
	void StepStage(int32 Direction);
	void RefreshCarousel();
	void AnimateCarousel(float DeltaTime);
	/** カードのスライド開始からの経過時間（秒）。 */
	float CarouselElapsed = 0.f;
	/** カードを送る向きを示す符号。 */
	int32 SlideDirection = 0;

	UFUNCTION()
	void SelectStage(int32 Index);

	UFUNCTION()
	void ChooseStage(int32 Index);

	UFUNCTION()
	void PreviousStage();

	UFUNCTION()
	void NextStage();

	/** ステージ選択・解放・進捗の表示データを管理するPresenter。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarStageSelectPresenter> Presenter;

	/** リスト表示でステージ選択に使う項目一覧。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoarLoadoutEntry>> StageEntries;

	/** ステージ候補を一覧表示するコンテナ。 */
	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> StageList;

	/** 選択中のステージの解放やクリア状態を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StageStatus;

	/** 選択中のステージの収集進捗を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StageProgress;

	/** 前のステージへ移動するボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> PreviousButton;

	/** 次のステージへ移動するボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> NextButton;

	/** 表示直後のフォーカス設定を次のTickに持ち越すか。 */
	bool bInitialFocusPending = false;

	UFUNCTION()
	void OpenEncyclopedia();

	UFUNCTION()
	void FocusEncyclopedia();

	/** 図鑑画面を開くボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> EncyclopediaButton;

	void ResolveWidgetReferences();
	void UpdateActionHighlight();
	void RefreshStatusBadge(bool bCleared);
	/** 開始ボタンが直近にホバー状態だったか。 */
	bool bStartHovered = false;
	/** キャンセルボタンが直近にホバー状態だったか。 */
	bool bCancelHovered = false;
	/** 現在強調している画面操作。 */
	int32 HighlightedAction = INDEX_NONE - 1;

	UFUNCTION()
	void HandleStartStageClicked();

	UFUNCTION()
	void HandleCancelClicked();

	/** 選択中のステージ名を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StageNameText;

	/** 選択中のステージの説明を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StageDescriptionText;

	/** クリアに必要な捕獲目標数を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TargetCaptureCountText;

	/** 選択中のステージのサムネイルを表示する画像。 */
	UPROPERTY(Transient)
	TObjectPtr<UImage> StageThumbnailImage;

	/** 選択中のステージへ出発するボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> StartStageButton;

	/** ステージ選択を閉じるボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelButton;
};
