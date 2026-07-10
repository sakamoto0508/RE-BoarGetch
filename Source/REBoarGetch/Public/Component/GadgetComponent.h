// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GadgetComponent.generated.h"

class AGadgetBase;
class APawn;

/// <summary>
/// ガチャメカの装備・切り替え・使用をまとめるコンポーネントです。
/// Unity で武器管理を Player 本体に全部書かずに分ける感覚と同じです。
/// </summary>
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UGadgetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGadgetComponent();

	/// <summary>
	/// ガチャメカを装備します。
	/// 成功ならtrue,失敗ならfalseを返します。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool EquipGadget(TSubclassOf<AGadgetBase> GadgetClass);
	
	/// <summary>
	/// 現在のガチャメカを外します。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	void UnequipGadget();
	
	/// <summary>
	/// 現在装備中のガチャメカを使います。
	/// 成功ならtrue,失敗ならfalseを返します。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool UseCurrentGadget();
	
	/// <summary>
	/// ガチャメカを切り替えます。
	/// 成功ならtrue,失敗ならfalseを返します。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Gadget")
	bool SwitchGadget(TSubclassOf<AGadgetBase> NewGadgetClass);
	
	/// <summary>
	/// 現在のガチャメカを返します。
	/// CurrentGadget を TObjectPtr で保持して、Getter を AGadgetBase で返すのは、所有と公開を分けるため。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "Gadget")
	AGadgetBase* GetCurrentGadget() const { return CurrentGadget; }
	
protected:
	// Called when the game starts
	virtual void BeginPlay() override;

private:
	/// <summary>
	/// 初期装備のクラスです。
	/// BP から差し替えられるようにしています。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, Category = "Gadget")
	TSubclassOf<AGadgetBase> DefaultGadgetClass;

	/// <summary>
	/// 現在の装備です。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AGadgetBase> CurrentGadget;

	/// <summary>
	/// 所有者です。
	/// プレイヤーキャラに紐づけて使います。
	/// </summary>
	TObjectPtr<APawn> OwningPawn;
};
