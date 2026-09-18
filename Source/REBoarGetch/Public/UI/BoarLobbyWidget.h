#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarLobbyWidget.generated.h"

class UButton;
class UImage;
class UStageConfig;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyStageStartRequested, UStageConfig*, StageConfig);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLobbyStageSelectionClosed);

/** ステージ選択、装備変更、解放確認を行うロビーUIの基底Widgetです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarLobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 指定ステージの表示内容を更新し、決定操作を受け付けます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Lobby")
	void ShowStageSelection(UStageConfig* StageConfig);

	/** ステージ選択表示を閉じます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Lobby")
	void HideStageSelection();

	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Lobby")
	FOnLobbyStageStartRequested OnStageStartRequested;

	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Lobby")
	FOnLobbyStageSelectionClosed OnStageSelectionClosed;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

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
	void ResolveWidgetReferences();

	UFUNCTION()
	void HandleStartStageClicked();

	UFUNCTION()
	void HandleCancelClicked();

	UPROPERTY(Transient)
	TObjectPtr<UStageConfig> SelectedStageConfig;

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
