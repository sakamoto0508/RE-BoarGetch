// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GadgetComponent.generated.h"

class AGadgetBase;
class APawn;
enum class EGadgetUseStyle : uint8;

/**
 * ガチャメカの装備・切り替え・使用をまとめるコンポーネントです。
 * Unity で武器管理を Player 本体に全部書かずに分ける感覚と同じです。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UGadgetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGadgetComponent();

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
	 * 現在装備中のガチャメカを使います。
	 * 成功ならtrue,失敗ならfalseを返します。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool UseCurrentGadget();

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

	/**
	 * 指定スロット(0-3)のガジェットへ切り替えます。
	 */
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool SwitchGadgetBySlot(int32 SlotIndex);
	
	/**
	 * 現在のガチャメカを返します。
	 * CurrentGadget を TObjectPtr で保持して、Getter を AGadgetBase で返すのは、所有と公開を分けるため。
	 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	AGadgetBase* GetCurrentGadget() const { return CurrentGadget; }

	UFUNCTION(BlueprintPure, Category = "Gadget")
	int32 GetCurrentGadgetSlotIndex() const { return CurrentGadgetSlotIndex; }

	/**
	 * 現在ガジェットの使用タイプを返します。
	 * 未装備時は OneShot 扱いを返します。
	 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	EGadgetUseStyle GetCurrentGadgetUseStyle() const;
	
protected:
	// Called when the game starts
	virtual void BeginPlay() override;

private:
	static constexpr int32 MaxGadgetSlots = 4;

	bool IsValidSlotIndex(int32 SlotIndex) const;
	int32 FindFirstValidSlot() const;
	void InitializeDefaultSlots();

	/**
	 * 旧互換用の初期装備クラスです（DefaultGadgetSlotsが空の場合のみ使用）。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gadget")
	TSubclassOf<AGadgetBase> DefaultGadgetClass;

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
