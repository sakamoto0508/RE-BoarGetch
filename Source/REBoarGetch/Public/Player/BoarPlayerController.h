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
	///<summary> 入力設定を行う。///</summary>
	virtual void SetupInputComponent() override;

private:
	//-------------------------------------------------
	// Input Mapping
	//-------------------------------------------------

	///<summary> 
	///	使用するInput Mapping Context
	///	BPで設定する。
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	//-------------------------------------------------
	// Input Actions
	//-------------------------------------------------
	
	///<summary>
	///	移動入力（Axis2D）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> MoveAction;
	
	///<summary>
	///	カメラ入力(Axis2D)
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> LookAction;

	
	///<summary>
	///	ジャンプ入力（Digital）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> JumpAction;

	///<summary>
	///	ガジェット入力（Digital）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetAction;

	///<summary>
	///	インタラクト入力（Digital）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> InteractAction;

	///<summary>
	///	ダッシュ入力（Digital/Hold）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> DashAction;

	///<summary>
	///	ガジェット切替モディファイア入力（R1想定）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetModifierAction;

	///<summary>
	///	ガジェットスロット1入力（□想定）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot1Action;

	///<summary>
	///	ガジェットスロット2入力（△想定）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot2Action;

	///<summary>
	///	ガジェットスロット3入力（○想定）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot3Action;

	///<summary>
	///	ガジェットスロット4入力（×想定）
	///</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Input",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> GadgetSlot4Action;

	//-------------------------------------------------
	// Input Functions
	//-------------------------------------------------
	
	///<summary>
	///	移動入力を受け取る。
	///</summary>
	void Move(const FInputActionValue& Value);
	
	///<summary>
	///	カメラ入力を受け取る。
	///</summary>
	void Look(const FInputActionValue& Value);

	///<summary>
	///	ジャンプ開始入力を受け取る。
	///</summary>
	void JumpStarted();

	///<summary>
	///	ジャンプ終了入力を受け取る。
	///</summary>
	void JumpCompleted();

	///<summary>
	///	ガジェット使用入力を受け取る。
	///</summary>
	void UseGadget();

	///<summary>
	///	インタラクト入力を受け取る。
	///</summary>
	void Interact();

	///<summary>
	///	ダッシュ開始入力を受け取る。
	///</summary>
	void DashStarted();

	///<summary>
	///	ダッシュ終了入力を受け取る。
	///</summary>
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
	///<summary>
	///	現在操作しているPlayerCharacterを取得。
	///</summary>
	ABoarPlayerCharacter* GetBoarCharacter() const;

	bool bIsGadgetModifierHeld = false;
};
