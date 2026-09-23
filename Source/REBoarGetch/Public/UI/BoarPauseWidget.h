#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarPauseWidget.generated.h"

class UButton;

/** 見た目はWidget Blueprint、時間停止と復帰はControllerが管理します。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarPauseWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void FocusInitialChoice();
	void FocusLoadoutChoice();
	void FocusLobbyChoice();
protected:
	virtual void NativeConstruct() override;
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings") FName ResumeButtonName;
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings") FName LobbyButtonName;
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings") FName LoadoutButtonName;
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings") FName SettingsButtonName;
private:
	UFUNCTION() void OpenSettings();
	UFUNCTION() void FocusSettings();
	UPROPERTY(Transient) TObjectPtr<UButton> SettingsButton;
	UFUNCTION() void Resume();
	UFUNCTION() void LeaveStage();
	UFUNCTION() void FocusResume();
	UFUNCTION() void FocusLobby();
	UFUNCTION() void OpenLoadout();
	UFUNCTION() void FocusLoadout();
	UPROPERTY(Transient) TObjectPtr<UButton> ResumeButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LobbyButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LoadoutButton;
};
