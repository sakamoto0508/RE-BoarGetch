#include "Gadget/GadgetBase.h"
#include "Component/GadgetComponent.h"
#include "Interaction/AttackActivatable.h"
#include "TimerManager.h"
#include "Engine/World.h"

AGadgetBase::AGadgetBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AGadgetBase::SetStageStopped(bool bStopped)
{
	if (GetOwningGadgetComponent())
		return;

	if (bStopped)
		GetWorldTimerManager().PauseTimer(CooldownTimerHandle);
	else
		GetWorldTimerManager().UnPauseTimer(CooldownTimerHandle);
}

/**
 * ガチャメカを使用します。
 * 派生クラスで個別の効果を追加するための入口です。
 */
void AGadgetBase::Use_Implementation(AActor* TargetActor)
{
	if (!CanUse())
		return;

	(void) TargetActor;
	StartCooldown();
}

/**
 * 使用開始時の共通処理です。
 * 既存ガジェット互換のため、内部でUse()を呼び出して効果処理を実行します。
 */
void AGadgetBase::BeginUse_Implementation(AActor* TargetActor)
{
	if (!CanUse() || bIsUsing)
		return;

	bIsUsing = true;
	Use(TargetActor);

	// OneShotは開始と同時に使用完了扱いにする。
	if (UseStyle == EGadgetUseStyle::OneShot)
	{
		bIsUsing = false;
	}
}

/**
 * 使用終了時の共通処理です。
 * Hold系は終了時点でクールタイムを開始します。
 */
void AGadgetBase::EndUse_Implementation(AActor* TargetActor)
{
	(void)TargetActor;

	if (!bIsUsing)
		return;

	bIsUsing = false;

	if (UseStyle == EGadgetUseStyle::Hold && CanUse())
	{
		StartCooldown();
	}
}

/**
 * クールタイムを開始します。
 */
void AGadgetBase::StartCooldown()
{
	if (CooldownSeconds <= 0.0f)
		return;

	if (UGadgetComponent* Gadgets = GetOwningGadgetComponent())
	{
		Gadgets->StartGadgetCooldown(this, CooldownSeconds);
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
		return;

	// Componentを持たない所有者向け。終了は「Timerが存在しない」ことで判定するので、終了処理は空でよい。
	World->GetTimerManager().ClearTimer(CooldownTimerHandle);
	World->GetTimerManager().SetTimer(CooldownTimerHandle, FTimerDelegate::CreateWeakLambda(this, [] {}), CooldownSeconds, false);
}

bool AGadgetBase::CanUse() const
{
	if (const UGadgetComponent* Gadgets = GetOwningGadgetComponent())
		return !Gadgets->IsGadgetOnCooldown(this);

	const UWorld* World = GetWorld();
	return !World || !World->GetTimerManager().TimerExists(CooldownTimerHandle);
}

UGadgetComponent* AGadgetBase::GetOwningGadgetComponent() const
{
	const AActor* GadgetOwner = GetOwner();
	return GadgetOwner ? GadgetOwner->FindComponentByClass<UGadgetComponent>() : nullptr;
}

/**
 * 攻撃ヒットしたActorが攻撃起動I/Fを実装していれば通知を送ります。
 */
void AGadgetBase::TryActivateByAttack(AActor* HitActor, AActor* AttackInstigator)
{
	if (HitActor == nullptr || !HitActor->GetClass()->ImplementsInterface(UAttackActivatable::StaticClass()))
		return;

	IAttackActivatable::Execute_ActivateByAttack(HitActor, AttackInstigator, this);
}
