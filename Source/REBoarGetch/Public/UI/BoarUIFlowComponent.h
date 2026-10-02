#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SlateWrapperTypes.h"
#include "BoarUIFlowComponent.generated.h"

class ABoarPlayerController;
class UUserWidget;
class UWidget;
class UBoarPauseWidget;
class UBoarLoadoutWidget;
class UBoarSettingsWidget;
class UBoarEncyclopediaWidget;
class UBoarConfirmationWidget;
class UBoarArchiveSelectionWidget;

/** Local menu lifetime, nesting, focus and menu input policy. Stage-end UI stays in Controller. */
UCLASS()
class REBOARGETCH_API UBoarUIFlowComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	void Shutdown();
	bool IsPauseMenuOpen() const
	{
		return PauseWidget != nullptr;
	}
	bool BlocksGameplayInput() const;
	void TogglePauseMenu();
	void HandleMenuBack();
	void OpenLoadoutMenu();
	UFUNCTION() void CloseLoadoutMenu();
	void OpenEncyclopedia(UUserWidget *ParentMenu, UWidget *ReturnFocus);
	UFUNCTION() void CloseEncyclopedia();
	void CloseEncyclopediaFrom(UUserWidget *ParentMenu);
	void OpenSettingsMenu(UUserWidget *ParentMenu, UWidget *ReturnFocus);
	UFUNCTION() void CloseSettingsMenu();
	void OpenPauseMenu();
	void ResumeFromPause();
	void LeaveStageFromPause();
	void RemoveLobbyExitConfirmation();
	UFUNCTION() void ResolveLobbyExit(bool bConfirmed);
	void OpenFacilityMenu(bool bArchive);
	void OpenArchiveDisplaySelection();
	UFUNCTION() void CloseArchiveDisplaySelection();
	void CloseFacilityMenuInput();

  private:
	ABoarPlayerController *GetController() const;
	UPROPERTY(Transient) TObjectPtr<UBoarArchiveSelectionWidget> ArchiveSelectionWidget;
	UPROPERTY(Transient) TObjectPtr<UBoarEncyclopediaWidget> EncyclopediaWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> EncyclopediaParent;
	UPROPERTY(Transient) TObjectPtr<UWidget> EncyclopediaReturnFocus;
	UPROPERTY(Transient) TObjectPtr<UBoarConfirmationWidget> LobbyExitConfirmation;
	UPROPERTY(Transient) TObjectPtr<UBoarSettingsWidget> SettingsWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> SettingsParent;
	UPROPERTY(Transient) TObjectPtr<UWidget> SettingsReturnFocus;
	UPROPERTY(Transient) TObjectPtr<UBoarLoadoutWidget> LoadoutWidget;
	UPROPERTY(Transient) TObjectPtr<UBoarPauseWidget> PauseWidget;
	ESlateVisibility EncyclopediaParentVisibility = ESlateVisibility::Visible;
	ESlateVisibility SettingsParentVisibility = ESlateVisibility::SelfHitTestInvisible;
	bool bFacilityMenuOpen = false;
	bool bFacilityAddedUIContext = false;
	bool bEncyclopediaGameAndUI = false;
	bool bEncyclopediaAddedUIContext = false;
};
