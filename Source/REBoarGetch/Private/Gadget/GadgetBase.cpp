#include "Gadget/GadgetBase.h"
#include "Interaction/AttackActivatable.h"
#include "TimerManager.h"
#include "Engine/World.h"

AGadgetBase::AGadgetBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AGadgetBase::BeginPlay()
{
	Super::BeginPlay();
}

/// <summary>
/// ガチャメカを使用します。
/// 派生クラスで個別の効果を追加するための入口です。
/// </summary>
void AGadgetBase::Use_Implementation(AActor* TargetActor)
{
	if (!CanUse()) return;
	
	(void) TargetActor;
	StartCooldown();
}

/// <summary>
/// 使用開始時の共通処理です。
/// 既存ガジェット互換のため、内部でUse()を呼び出して効果処理を実行します。
/// </summary>
void AGadgetBase::BeginUse_Implementation(AActor* TargetActor)
{
	if (!CanUse() || bIsUsing)
	{
		return;
	}

	bIsUsing = true;
	Use(TargetActor);

	// OneShotは開始と同時に使用完了扱いにする。
	if (UseStyle == EGadgetUseStyle::OneShot)
	{
		bIsUsing = false;
	}
}

/// <summary>
/// 使用終了時の共通処理です。
/// Hold系は終了時点でクールタイムを開始します。
/// </summary>
void AGadgetBase::EndUse_Implementation(AActor* TargetActor)
{
	(void)TargetActor;
	if (!bIsUsing)
	{
		return;
	}

	bIsUsing = false;
	if (UseStyle == EGadgetUseStyle::Hold && !bIsOnCooldown)
	{
		StartCooldown();
	}
}

/// <summary>
/// クールタイムを開始します。
/// </summary>
void AGadgetBase::StartCooldown()
{
	if (CooldownSeconds <= 0.0f) return;
	
	UWorld* World = GetWorld();
	if (World == nullptr) return;
	
	bIsOnCooldown = true;
	
	World->GetTimerManager().ClearTimer(CooldownTimerHandle);
	World->GetTimerManager().SetTimer(
		CooldownTimerHandle,
		this,
		&AGadgetBase::FinishCooldown,
		CooldownSeconds,
		false
	);
}

/// <summary>
/// クールタイム終了処理です。
/// </summary>
void AGadgetBase::FinishCooldown()
{
	bIsOnCooldown = false;
}

/// <summary>
/// 攻撃ヒットしたActorが攻撃起動I/Fを実装していれば通知を送ります。
/// </summary>
void AGadgetBase::TryActivateByAttack(AActor* HitActor, AActor* AttackInstigator)
{
	if (HitActor == nullptr || !HitActor->GetClass()->ImplementsInterface(UAttackActivatable::StaticClass()))
	{
		return;
	}

	IAttackActivatable::Execute_ActivateByAttack(HitActor, AttackInstigator, this);
}
