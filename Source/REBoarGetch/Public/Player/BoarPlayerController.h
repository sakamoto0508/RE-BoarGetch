#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "BoarPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UBoarHUDWidget;
class UBoarResultWidget;
class UHealthComponent;
class UGadgetComponent;
class ABoarPlayerCharacter;

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
	void HandleStageCleared(int32 CapturedCount, int32 TargetCount);

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
	//-------------------------------------------------
	// Gameplay HUD
	//-------------------------------------------------

	/** ゲームプレイ中に常時表示するHUD Widget Blueprintクラスです。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|UI")
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

	//-------------------------------------------------
	// Result UI
	//-------------------------------------------------

	/** ステージクリア時に表示するWidget Blueprintクラスです。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|UI")
	TSubclassOf<UBoarResultWidget> ResultWidgetClass;

	/** 現在表示中のリザルトWidgetです。多重生成防止にも使用します。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarResultWidget> ResultWidget;

	/** リザルト終了時に戻るLobby Level名です。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|UI")
	FName LobbyLevelName = TEXT("L_Lobby");

	/** Result表示中にEnhanced Inputの各入口を遮断するフラグです。 */
	bool bResultScreenActive = false;

	/** 任意入力の連打による複数OpenLevel要求を防ぎます。 */
	bool bResultTransitionRequested = false;

	UFUNCTION()
	void ReturnToLobby();

	//-------------------------------------------------
	// Input Mapping
	//-------------------------------------------------

	/** BeginPlay時にPriority 0で登録するMapping Contextです。Controller Blueprintで設定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	//-------------------------------------------------
	// Input Actions
	//-------------------------------------------------
	
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

	//-------------------------------------------------
	// Input Functions
	//-------------------------------------------------
	
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

	//-------------------------------------------------
	// Utility
	//-------------------------------------------------

	/** 現在Possess中のPawnをBoarPlayerCharacterとして取得します。キャッシュせずPossess変更へ追従します。 */
	ABoarPlayerCharacter* GetBoarCharacter() const;

	/** ガジェット切替モディファイアを押している間だけtrueになります。 */
	bool bIsGadgetModifierHeld = false;
};
