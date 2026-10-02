#include "Core/BoarCaptureResolution.h"
#include "Boar/BoarBase.h"
#include "Cage/Cage.h"
#include "Item/HealPickup.h"
#include "Stage/StageRunData.h"
#include "EngineUtils.h"
#include "Engine/World.h"

ACage *BoarCaptureResolution::FindDestination(UWorld *World, const FVector &CaptureLocation)
{
	if (!World)
		return nullptr;
	ACage *NearestHealthy = nullptr;
	ACage *NearestDestroyed = nullptr;
	for (TActorIterator<ACage> It(World); It; ++It)
	{
		ACage *&Candidate = It->GetIsCageDestroyed() ? NearestDestroyed : NearestHealthy;
		const double Distance = FVector::DistSquared(CaptureLocation, It->GetActorLocation());
		// 同距離だけは既存Actorのパス順で安定化します。永続IDには使用しません。
		if (!Candidate || Distance < FVector::DistSquared(CaptureLocation, Candidate->GetActorLocation()) ||
			(Distance == FVector::DistSquared(CaptureLocation, Candidate->GetActorLocation()) &&
			 It->GetPathName() < Candidate->GetPathName()))
			Candidate = *It;
	}
	return NearestHealthy ? NearestHealthy : NearestDestroyed;
}
void BoarCaptureResolution::Resolve(UWorld *World, ABoarBase *Boar, FStageRunData &Run, TSubclassOf<AHealPickup> HealPickupClass,
									float HealItemDropChance)
{
	if (!World || !IsValid(Boar))
		return;
	const FVector CaptureLocation = Boar->GetActorLocation();
	ACage *Destination = FindDestination(World, CaptureLocation);
	if (!Boar->BoarUniqueId.IsNone())
		Run.CapturedBoarUniqueIds.AddUnique(Boar->BoarUniqueId);
	else
		UE_LOG(LogTemp, Warning, TEXT("[Stage] %s has no persistent BoarUniqueId."), *GetNameSafe(Boar));
	if (ACage *Cage = Destination)
		Cage->CollectBoar(Boar);
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Stage] No cage exists; captured boar is released."));
		Boar->ReleaseBoar();
	}
	// 檻へ移動した後の座標ではなく、捕獲地点にドロップします。
	if (HealPickupClass && FMath::FRand() <= HealItemDropChance)
		World->SpawnActor<AHealPickup>(HealPickupClass, CaptureLocation + FVector(0, 0, 40), FRotator::ZeroRotator);
}
