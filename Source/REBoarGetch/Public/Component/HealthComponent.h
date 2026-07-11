// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChangedSignature, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeathSignature);

/// <summary>
/// HP を Character から切り離すためのコンポーネントです。
/// Unity でいうと、HP ロジック専用の MonoBehaviour に近い役割です。
/// </summary>
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	///<summary> ダメージを受けます	///</sumarry>
	UFUNCTION(BlueprintCallable, Category="Health")
	void TakeDamage(float DamageAmount);
	
	///<summary> HPを回復します	///</sumarry>
	UFUNCTION(BlueprintCallable, Category="Health")
	void Heal(float HealAmount);
	
	///<summary> HPReset	///</summary>
	UFUNCTION(BlueprintCallable, Category="Health")
	void ResetHealth();
	
	/// <summary> 死亡状態かどうかを返します。</summary>
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const;

	/// <summary> 現在 HP を返します。</summary>
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	/// <summary> 最大 HP を返します。</summary>
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	/// <summary> HP変化通知イベントです。 UI 更新に使いやすくしてあります。</summary>
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthChangedSignature OnHealthChanged;

	/// <summary> 死亡通知イベントです。</summary>
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDeathSignature OnDeath;

protected:
	virtual void BeginPlay() override;

private:
	/// <summary>
	/// 最大 HP です。
	/// BP で調整できるようにしています。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	/// <summary>現在 HP です。</summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 100.0f;

	/// <summary>HP を範囲内に収めて通知します。</summary>
	void ClampAndBroadcastHealth();
};