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
	/** 装備画面から戻った際、開く前の操作へフォーカスを戻します。 */
	void FocusLoadoutChoice();
	void FocusLobbyChoice();

protected:
	virtual void NativeConstruct() override;

	/** WBPで再開ボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings")
	FName ResumeButtonName;

	/** WBPでロビーへ戻るボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings")
	FName LobbyButtonName;

	/** WBPで装備変更ボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings")
	FName LoadoutButtonName;

	/** WBPで設定ボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Pause|Bindings")
	FName SettingsButtonName;

private:
	UFUNCTION()
	void OpenSettings();

	UFUNCTION()
	void FocusSettings();

	/** ゲームの設定画面を開くボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> SettingsButton;

	UFUNCTION()
	void Resume();

	UFUNCTION()
	void LeaveStage();

	UFUNCTION()
	void FocusResume();

	UFUNCTION()
	void FocusLobby();

	UFUNCTION()
	void OpenLoadout();

	UFUNCTION()
	void FocusLoadout();

	/** ポーズを解除してゲームへ戻るボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> ResumeButton;

	/** 挑戦を中断してロビーへ戻るボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> LobbyButton;

	/** ガジェット編成画面を開くボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> LoadoutButton;
};
