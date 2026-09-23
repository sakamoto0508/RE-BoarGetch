// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GadgetComponent.generated.h"

class AGadgetBase;
class APawn;
enum class EGadgetUseStyle : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGadgetLoadoutChanged);

/** 
 * プレイヤーが所持するガジェットを管理するComponentです。 
 * 主に以下の処理を担当します。 
 *  ・ガジェットの生成と装備 
 *  ・現在のガジェットの破棄 
 *  ・ガジェット使用の開始と終了 
 *  ・最大4つのガジェットスロットの管理
 *  ・指定スロットへの装備切り替え 
 *  ・ゲーム開始時の初期ガジェット装備 * 
 *  ガジェットに関する処理をPlayerCharacterから分離することで、
 *  プレイヤー本体の責務が増えすぎないようにしています。 
 *  
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UGadgetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	/** Tickを使用しないガジェット管理Componentの初期状態を構築します。 */
	UGadgetComponent();
	/** スロット仕様値の唯一の取得元です。保存済み4枠の仕様は変更しません。 */
	static constexpr int32 GetGadgetSlotCount() { return MaxGadgetSlots; }

	/**
	 * ガチャメカを装備します。
	 * 成功ならtrue,失敗ならfalseを返します。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool EquipGadget(TSubclassOf<AGadgetBase> GadgetClass);
	
	/**
	 * 現在のガチャメカを外します。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	void UnequipGadget();
	
	/**
	 * 現在装備中のガジェット使用を開始します。
	 * Hold系はこの開始状態を維持します。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool BeginUseCurrentGadget();

	/**
	 * 現在装備中のガジェット使用を終了します。
	 * OneShotは通常ほぼ何もしません。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool EndUseCurrentGadget();
	
	/**
	 * ガチャメカを切り替えます。
	 * 成功ならtrue,失敗ならfalseを返します。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool SwitchGadget(TSubclassOf<AGadgetBase> NewGadgetClass);

	/**
	 * 指定スロット(0-3)へガジェットを設定します。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool SetGadgetSlot(int32 SlotIndex, TSubclassOf<AGadgetBase> GadgetClass);
	/** UIが実行時の変更成功と永続保存成功を区別するための結果です。 */
	bool WasLastLoadoutSaveSuccessful() const { return bLastLoadoutSaveSuccessful; }

	/**
	 * 指定スロット(0-3)のガジェットへ切り替えます。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool SwitchGadgetBySlot(int32 SlotIndex);

	/** スロット内容または選択中スロットが変わったときに通知します。 */
	UPROPERTY(BlueprintAssignable, Category = "Gadget")
	FOnGadgetLoadoutChanged OnGadgetLoadoutChanged;
	
	/**
	 * 現在のガチャメカを返します。
	 * CurrentGadget を TObjectPtr で保持して、Getter を AGadgetBase で返すのは、所有と公開を分けるため。
	 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	AGadgetBase* GetCurrentGadget() const { return CurrentGadget; }

	/** 現在選択中のガジェットスロット番号を返します。未選択時はINDEX_NONEです。 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	int32 GetCurrentGadgetSlotIndex() const { return CurrentGadgetSlotIndex; }

	/** 指定スロットに設定されているガジェットClassを返します。空または範囲外ならnullです。 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	TSubclassOf<AGadgetBase> GetGadgetSlotClass(int32 SlotIndex) const;

	/**
	 * 現在ガジェットの使用タイプを返します。
	 * 未装備時は OneShot 扱いを返します。
	 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	EGadgetUseStyle GetCurrentGadgetUseStyle() const;
	
protected:
	// Called when the game starts
	/** 所有Pawnを取得し、初期スロットと初期装備を準備します。 */
	virtual void BeginPlay() override;

private:
	bool SaveCurrentLoadout();
	bool bLastLoadoutSaveSuccessful = false;
	/** プレイヤーが使用できるガジェットスロットの最大数です。 */
	static constexpr int32 MaxGadgetSlots = 4;

	/** 指令された番号が有効なスロット範囲か確認します。*/
	bool IsValidSlotIndex(int32 SlotIndex) const;
	/** ガジェットが登録されている最初のスロットを検索しています。 */
	int32 FindFirstValidSlot() const;
	/** エディタで設定された初期ガジェットから実際に使用するスロット配列を構築します。 */
	void InitializeDefaultSlots();

	/**
	 * 初期装備スロットです。最大4件までを使用します。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gadget")
	TArray<TSubclassOf<AGadgetBase>> DefaultGadgetSlots;

	/**
	 * 現在装備スロット(0-3)です。
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	TArray<TSubclassOf<AGadgetBase>> EquippedGadgetSlots;

	/**
	 * 現在アクティブなスロット番号です。未選択時はINDEX_NONE。
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	int32 CurrentGadgetSlotIndex = INDEX_NONE;

	/**
	 * 現在の装備です。
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AGadgetBase> CurrentGadget;

	/**
	 * 所有者です。
	 * プレイヤーキャラに紐づけて使います。
	 */
	TObjectPtr<APawn> OwningPawn;
};
