// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

/** 現在HPと最大HPを購読先へ通知するデリゲート型です。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChangedSignature, float, CurrentHealth, float, MaxHealth);
/** HPが0へ到達したことを購読先へ通知するデリゲート型です。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeathSignature);

/**
 * HP を Character から切り離すためのコンポーネントです。
 * Unity でいうと、HP ロジック専用の MonoBehaviour に近い役割です。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Tickを使用しないHP管理Componentの初期状態を構築します。 */
	UHealthComponent();

	/** ダメージを受けます */
	UFUNCTION(BlueprintCallable, Category="Health")
	void TakeDamage(float DamageAmount);
	
	/** HPを回復します */
	UFUNCTION(BlueprintCallable, Category="Health")
	void Heal(float HealAmount);
	
	/** HPReset */
	UFUNCTION(BlueprintCallable, Category="Health")
	void ResetHealth();
	
	/** 死亡状態かどうかを返します。 */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const;

	/** 現在 HP を返します。 */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	/** 最大 HP を返します。 */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	/** HP変化通知イベントです。 UI 更新に使いやすくしてあります。 */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthChangedSignature OnHealthChanged;

	/** 死亡通知イベントです。 */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDeathSignature OnDeath;

protected:
	/** 現在HPを最大HPで初期化し、初期値を購読先へ通知します。 */
	virtual void BeginPlay() override;

private:
	/**
	 * 最大 HP です（プレイヤー仕様に合わせて5スタック想定）。
	 * BP で調整できるようにしています。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MaxHealth = 5.0f;

	/** 現在 HP です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 5.0f;

	/** HP を範囲内に収めて通知します。 */
	void ClampAndBroadcastHealth();
};
