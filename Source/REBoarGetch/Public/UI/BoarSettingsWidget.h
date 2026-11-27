#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarSettingsWidget.generated.h"

class UBoarAudioManagerSubsystem;
class USlider;
class UTextBlock;
class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoarSettingsClosed);

/** Title/Pause共用。親画面への復帰と入力モードは呼び出し元が管理します。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 設定画面を閉じたことを親画面へ通知する。 */
	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnBoarSettingsClosed OnClosed;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void FocusInitialChoice();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void RequestClose();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	/** WBPでBGM音量スライダーを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings")
	FName BGMSliderName;

	/** WBPでSE音量スライダーを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings")
	FName SESliderName;

	/** WBPでBGM音量の数値欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings")
	FName BGMValueTextName;

	/** WBPでSE音量の数値欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings")
	FName SEValueTextName;

	/** WBPで戻るボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings")
	FName BackButtonName;

	/** WBPで図鑑を開くボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings")
	FName EncyclopediaButtonName;

private:
	UFUNCTION()
	void OpenEncyclopedia();

	UFUNCTION()
	void FocusEncyclopedia();

	/** 設定画面から図鑑を開くボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> EncyclopediaButton;

	UFUNCTION()
	void ChangeBGM(float Value);

	UFUNCTION()
	void ChangeSE(float Value);

	UFUNCTION()
	void FocusBack();

	void RefreshValues();

	/** BGMとSEの現在音量の取得・設定先。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarAudioManagerSubsystem> Audio;

	/** BGM音量を調整するスライダー。 */
	UPROPERTY(Transient)
	TObjectPtr<USlider> BGMSlider;

	/** SE音量を調整するスライダー。 */
	UPROPERTY(Transient)
	TObjectPtr<USlider> SESlider;

	/** 現在のBGM音量を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BGMValueText;

	/** 現在のSE音量を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SEValueText;

	/** 設定画面を閉じるボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> BackButton;

	/** 表示への音量反映中にユーザー操作として扱わないためのフラグ。 */
	bool bRefreshing = false;
	/** 設定画面を開いた後に初期フォーカスを設定する必要があるか。 */
	bool bInitialFocusPending = true;
};
