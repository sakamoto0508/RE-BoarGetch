#include "Component/HealthComponent.h"

/// <summary>
/// コンストラクタです。
/// HP は毎フレーム更新しないので Tick は不要です。
/// </summary>
UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/// <summary>
/// 初期化処理です。
/// ゲーム開始時に HP を満タンに戻します。
/// </summary>
void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetHealth();
}

/// <summary>
/// ダメージを受けます。
/// </summary>
void UHealthComponent::TakeDamage(float DamageAmount)
{
	if (DamageAmount <= 0.0f || IsDead())	return;
	
	CurrentHealth -= DamageAmount;
	ClampAndBroadcastHealth();

	if (IsDead())	OnDeath.Broadcast();
}

/// <summary>
/// HP を回復します。
/// </summary>
void UHealthComponent::Heal(float HealAmount)
{
	if (HealAmount <= 0.0f || IsDead())	return;
	
	CurrentHealth += HealAmount;
	ClampAndBroadcastHealth();
}

/// <summary>
/// HP を最大値まで戻します。
/// </summary>
void UHealthComponent::ResetHealth()
{
	CurrentHealth = MaxHealth;
	ClampAndBroadcastHealth();
}

/// <summary>
/// 死亡状態かどうかを返します。
/// </summary>
bool UHealthComponent::IsDead() const
{
	return CurrentHealth <= 0.0f;
}

/// <summary>
/// HP を範囲内に収めて通知します。
/// </summary>
void UHealthComponent::ClampAndBroadcastHealth()
{
	CurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, MaxHealth);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}