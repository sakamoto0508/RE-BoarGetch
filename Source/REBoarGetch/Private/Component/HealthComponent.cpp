#include "Component/HealthComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "Player/BoarPlayerCharacter.h"

/**
 * コンストラクタです。
 * HP は毎フレーム更新しないので Tick は不要です。
 */
UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/**
 * 初期化処理です。
 * ゲーム開始時に HP を満タンに戻します。
 */
void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>();
	// StageのPlayer HPはBoar生成後にPlayer開始処理から初期化します。
	if (Cast<ABoarPlayerCharacter>(GetOwner()) && Mode && Mode->GetStageState() == EBoarStageState::Preparing) return;
	ResetHealth();
}

/**
 * ダメージを受けます。
 */
void UHealthComponent::TakeDamage(float DamageAmount)
{
	if (const ABoarGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
		if (!Mode->CanAdvanceStage()) return;
	if (DamageAmount <= 0.0f || IsDead())	return;
	
	CurrentHealth -= DamageAmount;
	ClampAndBroadcastHealth();

	if (IsDead())	OnDeath.Broadcast();
}

/**
 * HP を回復します。
 */
void UHealthComponent::Heal(float HealAmount)
{
	if (const ABoarGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
		if (!Mode->CanAdvanceStage()) return;
	if (HealAmount <= 0.0f || IsDead())	return;
	
	CurrentHealth += HealAmount;
	ClampAndBroadcastHealth();
}

/**
 * HP を最大値まで戻します。
 */
void UHealthComponent::ResetHealth()
{
	CurrentHealth = MaxHealth;
	ClampAndBroadcastHealth();
}

/**
 * 死亡状態かどうかを返します。
 */
bool UHealthComponent::IsDead() const
{
	return CurrentHealth <= 0.0f;
}

/**
 * HP を範囲内に収めて通知します。
 */
void UHealthComponent::ClampAndBroadcastHealth()
{
	CurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, MaxHealth);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}
