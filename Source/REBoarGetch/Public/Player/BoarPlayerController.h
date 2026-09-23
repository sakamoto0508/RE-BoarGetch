#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Components/SlateWrapperTypes.h"
#include "BoarPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UBoarHUDWidget;
class UBoarResultWidget;
class UBoarGameOverWidget;
class UBoarPauseWidget;
class UBoarLoadoutWidget;
class UBoarSettingsWidget;
class UBoarConfirmationWidget;
class UBoarEncyclopediaWidget;
class UUserWidget;
class UWidget;
class UHealthComponent;
class UGadgetComponent;
class ABoarPlayerCharacter;
struct FStageRunData;

/**
 * ローカルプレイヤーの入力と画面UIを仲介するControllerです。
 *
 * Enhanced InputのActionを受け取り、実際の移動・ジャンプ・ガジェット処理は
 * Possess中のABoarPlayerCharacterへ委譲します。また、Character/GameModeの変更通知を
 * HUDへ中継し、ステージクリア時だけゲーム入力からResult UI入力へ切り替えます。
 * ダメージ計算や捕獲条件などのゲームルールは、このクラスでは扱いません。
 */
UCLASS()
class REBOARGETCH_API ABoarPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** ステージクリア時に操作を停止し、リザルト画面を表示します。 */
	void HandleStageCleared(const FStageRunData& Run, int32 TargetCount);
	/** 終了演出からUIまでゲーム入力を遮断します。 */
	void SetStageInputBlocked(bool bBlocked);
	/**	 ステージクリア失敗時に操作を停止し、ゲームオーバー画面を表示します。 */
	void HandleStageGameOver();
	/**	 ステージクリア失敗時に操作を停止し、ゲームオーバー画面を表示します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Stage") void RetryStage();
	/** ポーズ画面を開き、ゲーム入力を遮断します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void OpenPauseMenu();
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void TogglePauseMenu();
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void OpenLoadoutMenu();
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void CloseLoadoutMenu();
	UFUNCTION() void HandleMenuBack();
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void OpenSettingsMenu(UUserWidget* ParentMenu, UWidget* ReturnFocus);
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void CloseSettingsMenu();
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void OpenEncyclopedia(UUserWidget* ParentMenu, UWidget* ReturnFocus);
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void CloseEncyclopedia();
	void CloseEncyclopediaFrom(UUserWidget* ParentMenu);
	/** ポーズ画面を閉じてゲーム入力へ戻す。*/
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void ResumeFromPause();
	/** ポーズ画面からLobby退出確認を開きます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|UI") void LeaveStageFromPause();
	/** ポーズメニューが開いているかどうかを返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|UI") bool IsPauseMenuOpen() const { return PauseWidget != nullptr; }

protected:
	/** 入力Mapping Contextをローカルプレイヤーへ登録します。 */
	virtual void BeginPlay() override;
	/** PossessしたプレイヤーのHP変更通知をHUDへ接続します。 */
	virtual void OnPossess(APawn* InPawn) override;
	/** PlayerController終了時にHUD用イベント購読を解除します。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 設定済みInput ActionをEnhanced Input Componentへ登録します。未設定Actionは安全に無視します。 */
	virtual void SetupInputComponent() override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "UI") TSubclassOf<UBoarEncyclopediaWidget> EncyclopediaWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UBoarEncyclopediaWidget> EncyclopediaWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> EncyclopediaParent;
	UPROPERTY(Transient) TObjectPtr<UWidget> EncyclopediaReturnFocus;
	ESlateVisibility EncyclopediaParentVisibility = ESlateVisibility::Visible;
	bool bEncyclopediaGameAndUI = false;
	bool bEncyclopediaAddedUIContext = false;
	UPROPERTY(EditDefaultsOnly, Category = "UI") TSubclassOf<UBoarConfirmationWidget> LobbyExitConfirmationClass;
	UPROPERTY(Transient) TObjectPtr<UBoarConfirmationWidget> LobbyExitConfirmation;
	UFUNCTION() void ResolveLobbyExit(bool bConfirmed);
	void RemoveLobbyExitConfirmation();
	UPROPERTY(EditDefaultsOnly, Category = "UI") TSubclassOf<UBoarSettingsWidget> SettingsWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UBoarSettingsWidget> SettingsWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> SettingsParent;
	UPROPERTY(Transient) TObjectPtr<UWidget> SettingsReturnFocus;
	ESlateVisibility SettingsParentVisibility = ESlateVisibility::SelfHitTestInvisible;
	UPROPERTY(EditDefaultsOnly, Category = "UI") TSubclassOf<UBoarLoadoutWidget> LoadoutWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UBoarLoadoutWidget> LoadoutWidget;
	/** ポーズメニューWidget Blueprintクラスです。 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UBoarPauseWidget> PauseWidgetClass;
	
	/** ポーズメニューWidgetのインスタンスです。 */
	UPROPERTY(Transient) TObjectPtr<UBoarPauseWidget> PauseWidget;
	
	/** ゲームオーバー画面Widget Blueprintクラスです。 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UBoarGameOverWidget> GameOverWidgetClass;
	
	/**	 現在表示中のゲームオーバーWidgetです。 */
	UPROPERTY(Transient) TObjectPtr<UBoarGameOverWidget> GameOverWidget;

	/** ゲームプレイ中に常時表示するHUD Widget Blueprintクラスです。 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UBoarHUDWidget> PlayerHUDWidgetClass;

	/** 現在表示中のゲームプレイHUDです。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarHUDWidget> PlayerHUDWidget;

	/** 現在購読中のHP Componentです。Possess切り替え時の解除に使用します。 */
	UPROPERTY(Transient)
	TObjectPtr<UHealthComponent> ObservedHealthComponent;

	/** 現在購読中のGadget Componentです。Possess切り替え時の解除に使用します。 */
	UPROPERTY(Transient)
	TObjectPtr<UGadgetComponent> ObservedGadgetComponent;

	/** ローカルControllerにHUDを一度だけ生成し、初期値の反映とイベント購読を行います。 */
	void CreatePlayerHUD();

	/** Level開始時にリザルト画面で設定した入力ロックを解除します。 */
	void RestoreGameplayInputState();

	/** HP通知の購読先を指定Characterへ付け替え、現在HPを即時反映します。 */
	void BindPlayerHealth(ABoarPlayerCharacter* PlayerCharacter);

	/** ガジェット通知の購読先を指定Characterへ付け替え、現在4スロットを即時反映します。 */
	void BindPlayerGadgets(ABoarPlayerCharacter* PlayerCharacter);

	/** 現在の4スロットと選択枠をHUDへ反映します。 */
	UFUNCTION()
	void HandleGadgetLoadoutChanged();

	/** 捕獲数変更通知をHUDへ反映します。 */
	UFUNCTION()
	void HandleCapturedBoarCountChanged(int32 CurrentCount, int32 TargetCount);

	/** HP変更通知をHUDへ反映します。 */
	UFUNCTION()
	void HandleHealthChanged(float CurrentHealth, float MaxHealth);

	/** ステージクリア時に表示するWidget Blueprintクラスです。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|UI")
	TSubclassOf<UBoarResultWidget> ResultWidgetClass;

	/** 現在表示中のリザルトWidgetです。多重生成防止にも使用します。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarResultWidget> ResultWidget;

	/** リザルト終了時に戻るLobby Level名です。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|UI")
	FName LobbyLevelName = TEXT("L_Lobby");
	/** 新規設定はAsset参照を使用。旧BPのLevel名は移行互換用に残します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|UI")
	TSoftObjectPtr<UWorld> LobbyLevel;

	/** Result表示中にEnhanced Inputの各入口を遮断するフラグです。 */
	bool bGameplayInputBlocked = false;

	/** 任意入力の連打による複数OpenLevel要求を防ぎます。 */
	bool bLevelTransitionRequested = false;
	bool CanProcessGameplayInput() const;
	void AddOwnedMappingContext(UInputMappingContext* Context, int32 Priority);
	void RemoveOwnedMappingContext(UInputMappingContext* Context);
	UPROPERTY(Transient) TArray<TObjectPtr<UInputMappingContext>> OwnedMappingContexts;
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputMappingContext> GlobalMappingContext;
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputMappingContext> UIMappingContext;

	UFUNCTION()
	void ReturnToLobby();

	/** BeginPlay時にPriority 0で登録するMapping Contextです。Controller Blueprintで設定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	
	/**
	 * 移動入力（Axis2D）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> MoveAction;
	
	/**
	 * カメラ入力(Axis2D)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> LookAction;

	
	/**
	 * ジャンプ入力（Digital）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> JumpAction;

	/**
	 * ガジェット入力（Digital）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetAction;

	/**
	 * ダッシュ入力（Digital/Hold）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> DashAction;

	/**
	 * ガジェット切替モディファイア入力（R1想定）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetModifierAction;

	/**
	 * ガジェットスロット1入力（□想定）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot1Action;

	/**
	 * ガジェットスロット2入力（△想定）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot2Action;

	/**
	 * ガジェットスロット3入力（○想定）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot3Action;

	/**
	 * ガジェットスロット4入力（×想定）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot4Action;
	
	/**
	 *	 ポーズ入力（Digital）
	 */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> PauseAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> UIBackAction;
	
	/**
	 * 移動入力を受け取る。
	 */
	void Move(const FInputActionValue& Value);
	
	/**
	 * カメラ入力を受け取る。
	 */
	void Look(const FInputActionValue& Value);

	/**
	 * ジャンプ開始入力を受け取る。
	 */
	void JumpStarted();

	/**
	 * ジャンプ終了入力を受け取る。
	 */
	void JumpCompleted();

	/**
	 * ガジェット使用開始入力を受け取る。
	 */
	void GadgetStarted();

	/**
	 * ガジェット使用終了入力を受け取る。
	 */
	void GadgetCompleted();

	/**
	 * ダッシュ開始入力を受け取る。
	 */
	void DashStarted();

	/**
	 * ダッシュ終了入力を受け取る。
	 */
	void DashCompleted();

	/** ガジェット切替モディファイアを押下状態にします。 */
	void GadgetModifierStarted();
	/** ガジェット切替モディファイアを解除状態にします。 */
	void GadgetModifierCompleted();
	/** モディファイア押下中であればスロット1へ切り替えます。 */
	void SwitchGadgetSlot1();
	/** モディファイア押下中であればスロット2へ切り替えます。 */
	void SwitchGadgetSlot2();
	/** モディファイア押下中であればスロット3へ切り替えます。 */
	void SwitchGadgetSlot3();
	/** モディファイア押下中であればスロット4へ切り替えます。 */
	void SwitchGadgetSlot4();
	/** 切替条件を満たす場合に指定スロットへの変更をCharacterへ依頼します。 */
	void TrySwitchGadgetSlot(int32 SlotIndex);
	
	/** 現在Possess中のPawnをBoarPlayerCharacterとして取得します。キャッシュせずPossess変更へ追従します。 */
	ABoarPlayerCharacter* GetBoarCharacter() const;

	/** ガジェット切替モディファイアを押している間だけtrueになります。 */
	bool bIsGadgetModifierHeld = false;
};
