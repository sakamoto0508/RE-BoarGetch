// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GadgetBase.generated.h"

/// <summary>
/// ガチャメカの基底クラスです。
/// Net / Radar / Jet などはここから派生します。
/// </summary>
UCLASS()
class AGadgetBase : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AGadgetBase();

	/// <summary>
	/// ガチャメカを使用します。
	/// 派生クラスで実際の効果を実装します。
	/// </summary>
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Boar|Gadget")
	void Use(AActor* TargetActor);

	/// <summary>
	/// C++ 側のデフォルト挙動です。
	/// </summary>
	virtual void Use_Implementation(AActor* TargetActor);

	/// <summary>
	/// 使用可能かどうかを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category="Gadget")
	bool CanUse() const { return !bIsOnCooldown; }
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	/// <summary>
	/// クールタイムです。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float CooldownSeconds = 1.0f;

	/// <summary>
	/// クールタイム中かどうかです。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bIsOnCooldown = false;

	/// <summary>
	/// クールタイム解除用のタイマーです。
	/// </summary>
	FTimerHandle CooldownTimerHandle;

	/// <summary>
	/// クールタイムを開始します。
	/// </summary>
	void StartCooldown();

	/// <summary>
	/// クールタイム終了処理です。
	/// </summary>
	void FinishCooldown();
};
