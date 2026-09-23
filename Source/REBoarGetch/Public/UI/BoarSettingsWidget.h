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
	UPROPERTY(BlueprintAssignable, Category = "Settings") FOnBoarSettingsClosed OnClosed;
	UFUNCTION(BlueprintCallable, Category = "Settings") void FocusInitialChoice();
	UFUNCTION(BlueprintCallable, Category = "Settings") void RequestClose();
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings") FName BGMSliderName;
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings") FName SESliderName;
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings") FName BGMValueTextName;
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings") FName SEValueTextName;
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings") FName BackButtonName;
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Bindings") FName EncyclopediaButtonName;
private:
	UFUNCTION() void OpenEncyclopedia();
	UFUNCTION() void FocusEncyclopedia();
	UPROPERTY(Transient) TObjectPtr<UButton> EncyclopediaButton;
	UFUNCTION() void ChangeBGM(float Value);
	UFUNCTION() void ChangeSE(float Value);
	UFUNCTION() void FocusBack();
	void RefreshValues();
	UPROPERTY(Transient) TObjectPtr<UBoarAudioManagerSubsystem> Audio;
	UPROPERTY(Transient) TObjectPtr<USlider> BGMSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> SESlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BGMValueText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SEValueText;
	UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
	bool bRefreshing = false;
	bool bInitialFocusPending = true;
};
