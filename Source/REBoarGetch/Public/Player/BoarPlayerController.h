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
class ABoarPlayerCharacter;

/**
 * @brief プレイヤーの入力を管理するクラス。
 * このクラスの責務
 * ・Enhanced Input の設定
 * ・入力の受付
 * ・PlayerCharacterへ入力を渡す
 *
 * このクラスではゲームロジックを書かない。
 */
UCLASS()
class REBOARGETCH_API ABoarPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Enhanced Inputを使用するプレイヤーControllerの初期状態を構築します。 */
	ABoarPlayerController();

	/** ステージクリア時に操作を停止し、リザルト画面を表示します。 */
	void HandleStageCleared(int32 CapturedCount, int32 TargetCount);

protected:
	/** 入力Mapping Contextをローカルプレイヤーへ登録します。 */
	virtual void BeginPlay() override;
	/** PossessしたプレイヤーのHP変更通知をHUDへ接続します。 */
	virtual void OnPossess(APawn* InPawn) override;
	/** PlayerController終了時にHUD用イベント購読を解除します。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/*
	 *SetupInputComponent()
	 * Enhanced Input の設定を行う。
	 * 入力が来たらMove()が呼ばれる。
	 */
	/** 入力設定を行う。 */
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

	/** HUDを生成し、現在値とイベント購読を設定します。 */
	void CreatePlayerHUD();

	/** Level開始時にリザルト画面で設定した入力ロックを解除します。 */
	void RestoreGameplayInputState();

	/** 指定CharacterのHealth ComponentをHUDへ接続します。 */
	void BindPlayerHealth(ABoarPlayerCharacter* PlayerCharacter);

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

	/** タイトルへ戻る際に開くLevel名です。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|UI")
	FName TitleLevelName;

	/** リザルト表示後のゲームプレイ入力を明示的に遮断します。 */
	bool bResultScreenActive = false;

	/** Level遷移要求の連打を防ぎます。 */
	bool bResultTransitionRequested = false;

	UFUNCTION()
	void RetryCurrentStage();

	UFUNCTION()
	void ReturnToTitle();

	//-------------------------------------------------
	// Input Mapping
	//-------------------------------------------------

	/**
	 * 使用するInput Mapping Context
	 * BPで設定する。
	 */
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

	/**
	 * GetPawn()を毎回Castするだけなので
	 * メンバ変数を持たずに済みます。
	 */
	/**
	 * 現在操作しているPlayerCharacterを取得。
	 */
	ABoarPlayerCharacter* GetBoarCharacter() const;

	/** ガジェット切替モディファイアを押している間だけtrueになります。 */
	bool bIsGadgetModifierHeld = false;
};
