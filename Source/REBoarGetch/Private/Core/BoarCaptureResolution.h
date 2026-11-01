#pragma once

#include "CoreMinimal.h"

class UWorld;
class ABoarBase;
class ACage;
class AHealPickup;
struct FStageRunData;

/** 檻収容・今回の捕獲記録・報酬抽選の担当。Stage状態・重複通知・数の再集計は呼び出し元が管理します。 */
namespace BoarCaptureResolution
{
	ACage* FindDestination(UWorld* World, const FVector& CaptureLocation);
	void Resolve(UWorld* World, ABoarBase* Boar, FStageRunData& Run, TSubclassOf<AHealPickup> HealPickupClass,
	             float HealItemDropChance);
} 
