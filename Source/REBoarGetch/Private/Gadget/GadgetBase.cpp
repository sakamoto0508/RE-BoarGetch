#include "Gadget/GadgetBase.h"
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
