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

	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Lobby")
	FOnLobbyStageSelectionClosed OnStageSelectionClosed;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Lobby Widget References") FName EncyclopediaButtonName;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select") TSubclassOf<UBoarLoadoutEntry> StageEntryClass;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select") bool bUseCarousel = false;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Preview") TObjectPtr<UMaterialInterface> PreviewMaterial;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select", meta=(ClampMin="0.05", ClampMax="0.5")) float CarouselDuration = .18f;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select") TArray<FKey> PreviousStageKeys = { EKeys::Left, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftShoulder };
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select") TArray<FKey> NextStageKeys = { EKeys::Right, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_RightShoulder };
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select") TArray<FKey> CancelKeys = { EKeys::Escape, EKeys::Gamepad_FaceButton_Right };
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings") FName StageListWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings") FName StageStatusWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings") FName StageProgressWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings") FName PreviousButtonName;
	UPROPERTY(EditDefaultsOnly, Category = "Stage Select|Bindings") FName NextButtonName;

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
	// 表示範囲内のミニチュアと描画用MaterialをStageIdで対応付けて保持します。
	UPROPERTY(Transient) TMap<FName, TObjectPtr<ABoarStagePreviewActor>> PreviewActors;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> PreviewBrushes;
	UFUNCTION() void RefreshSelectedStage();
	void FocusSelection();
	void StepStage(int32 Direction);
	void RefreshCarousel();
	void AnimateCarousel(float DeltaTime);
	float CarouselElapsed = 0.f;
	int32 SlideDirection = 0;
	UFUNCTION() void SelectStage(int32 Index);
	UFUNCTION() void ChooseStage(int32 Index);
	UFUNCTION() void PreviousStage();
	UFUNCTION() void NextStage();
	// 選択ルールと進捗の整形はPresenter、描画・入力・フォーカスはこのWidgetが担当します。
	UPROPERTY(Transient) TObjectPtr<UBoarStageSelectPresenter> Presenter;
	UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> StageEntries;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> StageList;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StageStatus;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StageProgress;
	UPROPERTY(Transient) TObjectPtr<UButton> PreviousButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NextButton;
	// 表示直後のフォーカス設定を次のTickまで遅らせます。
	bool bInitialFocusPending = false;
	UFUNCTION() void OpenEncyclopedia();
	UFUNCTION() void FocusEncyclopedia();
	UPROPERTY(Transient) TObjectPtr<UButton> EncyclopediaButton;
	void ResolveWidgetReferences();

	UFUNCTION()
	void HandleStartStageClicked();

	UFUNCTION()
	void HandleCancelClicked();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StageNameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StageDescriptionText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TargetCaptureCountText;

	UPROPERTY(Transient)
	TObjectPtr<UImage> StageThumbnailImage;

	UPROPERTY(Transient)
	TObjectPtr<UButton> StartStageButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelButton;
};
