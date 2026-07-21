#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "BoarPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
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
	ABoarPlayerController();

protected:
	virtual void BeginPlay() override;

	/*
	 *SetupInputComponent()
	 * Enhanced Input の設定を行う。
	 * 入力が来たらMove()が呼ばれる。
	 */
	/** 入力設定を行う。 */
	virtual void SetupInputComponent() override;

private:
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

	void GadgetModifierStarted();
	void GadgetModifierCompleted();
	void SwitchGadgetSlot1();
	void SwitchGadgetSlot2();
	void SwitchGadgetSlot3();
	void SwitchGadgetSlot4();
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

	bool bIsGadgetModifierHeld = false;
};
