#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SlateWrapperTypes.h"
#include "Audio/BoarAudioTypes.h"
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

	UFUNCTION()
	void CloseLoadoutMenu();

	void OpenEncyclopedia(UUserWidget *ParentMenu, UWidget *ReturnFocus);

	UFUNCTION()
	void CloseEncyclopedia();

	void CloseEncyclopediaFrom(UUserWidget *ParentMenu);
	void OpenSettingsMenu(UUserWidget *ParentMenu, UWidget *ReturnFocus);

	UFUNCTION()
	void CloseSettingsMenu();

	void OpenPauseMenu();
	void ResumeFromPause();
	void LeaveStageFromPause();
	void RemoveLobbyExitConfirmation();

	UFUNCTION()
	void ResolveLobbyExit(bool bConfirmed);

	void OpenFacilityMenu(bool bArchive);
	void OpenArchiveDisplaySelection();

	UFUNCTION()
	void CloseArchiveDisplaySelection();

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

	/** 現在開いている展示枠選択画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarArchiveSelectionWidget> ArchiveSelectionWidget;

	/** 現在開いている図鑑画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarEncyclopediaWidget> EncyclopediaWidget;

	/** 図鑑を閉じたときに表示を復元する親画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> EncyclopediaParent;

	/** 図鑑を閉じたときにフォーカスを戻すWidget。 */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> EncyclopediaReturnFocus;

	/** 現在開いているロビー退出確認画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarConfirmationWidget> LobbyExitConfirmation;

	/** 現在開いている設定画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarSettingsWidget> SettingsWidget;

	/** 設定を閉じたときに表示を復元する親画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SettingsParent;

	/** 設定を閉じたときにフォーカスを戻すWidget。 */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> SettingsReturnFocus;

	/** 現在開いているガジェット編成画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarLoadoutWidget> LoadoutWidget;

	/** 現在開いているポーズ画面。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarPauseWidget> PauseWidget;

	/** 表示中のステージ選択画面。Widgetの生成・破棄はAStageEntrance、入力状態はUIFlowが所有します。 */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> StageSelectionMenu;

	/** 図鑑を閉じるときに戻す親画面の表示状態。 */
	ESlateVisibility EncyclopediaParentVisibility = ESlateVisibility::Visible;
	/** 設定を閉じるときに戻す親画面の表示状態。 */
	ESlateVisibility SettingsParentVisibility = ESlateVisibility::SelfHitTestInvisible;
	/** 施設のメニューが入力モードを管理しているか。 */
	bool bFacilityMenuOpen = false;
	/** 施設メニューが閉じる際に解除すべきUI入力Contextを追加したか。 */
	bool bFacilityAddedUIContext = false;
	/** 図鑑の表示中にGame and UI入力モードを使うか。 */
	bool bEncyclopediaGameAndUI = false;
	/** 図鑑が閉じる際に解除すべきUI入力Contextを追加したか。 */
	bool bEncyclopediaAddedUIContext = false;

	/** 0より大きい間はUI SEを鳴らしません。親画面を閉じる処理が子画面を連鎖して閉じる間・Shutdown中に使います。 */
	int32 UISoundMute = 0;
	/** UI共通SE（Project Settings）を鳴らします。 */
	void PlayFlowSound(EBoarUISound Sound);
};
