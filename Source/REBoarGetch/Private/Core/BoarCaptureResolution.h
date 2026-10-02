#pragma once

#include "CoreMinimal.h"

class UWorld;
class ABoarBase;
class ACage;
class AHealPickup;
struct FStageRunData;

/** Physical capture resolution; caller owns stage guards, duplicate handling and count refresh. */
namespace BoarCaptureResolution
{
ACage *FindDestination(UWorld *World, const FVector &CaptureLocation);
void Resolve(UWorld *World, ABoarBase *Boar, FStageRunData &Run, TSubclassOf<AHealPickup> HealPickupClass, float HealItemDropChance);
} // namespace BoarCaptureResolution
