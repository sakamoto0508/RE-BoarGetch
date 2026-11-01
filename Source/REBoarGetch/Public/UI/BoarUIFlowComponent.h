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

/** ローカルメニューの生成・親子関係・フォーカス・入力切替を管理します。ステージ終了UIはControllerが担当します。 */
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
	/** ステージ選択画面を開けるか。他のメニュー表示中・Level遷移中は開きません。 */
	bool CanOpenStageSelection() const;
	/**
	 * ステージ選択画面（AStageEntranceが生成したLobby Widget）の表示に伴う入力状態を引き受けます。
	 * Gameplay入力の抑制、Widgetへのフォーカス、カーソル表示をUIFlowが所有します。同じ画面なら入力状態を再適用します。
	 */
	bool OpenStageSelection(UUserWidget* Menu);
	/**
	 * ステージ選択画面の入力状態を解放します（冪等。開いた画面と一致する場合だけ処理）。子画面（図鑑）も閉じます。
	 * bLevelEndingではLevel遷移・破棄へ引き継ぐため、Input Modeとカーソルを戻さずGameplay入力を止めたままにします。
	 * 通常の終了では、他のメニューが入力を必要としていなければGame Onlyへ戻します。
	 */
	void CloseStageSelection(UUserWidget* Menu, bool bLevelEnding);
	bool IsStageSelectionOpen() const { return StageSelectionMenu != nullptr; }
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
	/** 表示中のステージ選択画面。Widgetの生成・破棄はAStageEntrance、入力状態はUIFlowが所有します。 */
	UPROPERTY(Transient) TObjectPtr<UUserWidget> StageSelectionMenu;
	ESlateVisibility EncyclopediaParentVisibility = ESlateVisibility::Visible;
	ESlateVisibility SettingsParentVisibility = ESlateVisibility::SelfHitTestInvisible;
	bool bFacilityMenuOpen = false;
	// 自分が追加したContextだけを閉じる際に解除し、親画面の入力設定を維持します。
	bool bFacilityAddedUIContext = false;
	bool bEncyclopediaGameAndUI = false;
	bool bEncyclopediaAddedUIContext = false;
};
