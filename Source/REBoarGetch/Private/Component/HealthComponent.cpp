#include "Component/HealthComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"

// HPは被弾・回復・初期化でのみ変化するため、毎フレームの更新は不要。
UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeHealth();
}

bool UHealthComponent::InitializeHealth()
{
	// 初期化の再実行を回復処理として使わない。明示的な回復・Resetとは責務を分ける。
	if (bHealthInitialized)
		return true;
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.f)
		return false;

	ResetHealth();
	return true;
}

void UHealthComponent::TakeDamage(float DamageAmount)
{
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return;
	if (DamageAmount <= 0.0f || IsDead())
		return;

	CurrentHealth -= DamageAmount;
	ClampAndBroadcastHealth();

	if (IsDead())
		OnDeath.Broadcast();
}

void UHealthComponent::Heal(float HealAmount)
{
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return;
	if (HealAmount <= 0.0f || IsDead())
		return;

	CurrentHealth += HealAmount;
	ClampAndBroadcastHealth();
}

void UHealthComponent::ResetHealth()
{
	bHealthInitialized = true;
	CurrentHealth = MaxHealth;
	ClampAndBroadcastHealth();
}

bool UHealthComponent::IsDead() const
{
	return CurrentHealth <= 0.0f;
}

void UHealthComponent::ClampAndBroadcastHealth()
{
	CurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, MaxHealth);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}
