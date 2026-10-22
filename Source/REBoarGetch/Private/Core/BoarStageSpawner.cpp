#include "Core/BoarStageSpawner.h"

#include "Boar/BoarBase.h"
#include "BoarSaveGame.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Stage/BoarSpawnPoint.h"
#include "Stage/SpecialCoin.h"
#include "Stage/StageConfig.h"

bool BoarStageSpawner::SpawnConfiguredBoars(UWorld* World, const UStageConfig* StageConfig)
{
	// Spawn Entryが空なら、既存Levelに手置きされたBoarだけを使用する。
	if (!StageConfig || (StageConfig->BoarSpawnEntries.IsEmpty() && StageConfig->BoarSpawnDefinitions.IsEmpty()))
	{
		return true;
	}

	if (!World)
	{
		return false;
	}

	TArray<ABoarSpawnPoint*> SpawnPoints;
	for (TActorIterator<ABoarSpawnPoint> It(World); It; ++It)
	{
		SpawnPoints.Add(*It);
	}
	if (!StageConfig->BoarSpawnDefinitions.IsEmpty())
	{
		bool bSucceeded = true;
		// spawn point IDの重複を検出するために、IDをキーにしたマップを作成します。
		TMap<FName, ABoarSpawnPoint*> ById;
		// コピーしたIDを記録するためのセットを作成します。
		TSet<FName> DuplicatePoints;
		for (ABoarSpawnPoint* Point : SpawnPoints)
		{
			if (Point->SpawnPointId.IsNone()) continue;
			if (ById.Contains(Point->SpawnPointId)) DuplicatePoints.Add(Point->SpawnPointId);
			ById.Add(Point->SpawnPointId, Point);
		}
		TMap<FName, int32> IdCounts;
		for (const FBoarSpawnDefinition& Entry : StageConfig->BoarSpawnDefinitions)
			++IdCounts.FindOrAdd(Entry.BoarUniqueId);
		for (TActorIterator<ABoarBase> It(World); It; ++It)
			if (!It->BoarUniqueId.IsNone()) ++IdCounts.FindOrAdd(It->BoarUniqueId);
		for (const FBoarSpawnDefinition& Entry : StageConfig->BoarSpawnDefinitions)
		{
			ABoarSpawnPoint* Point = ById.FindRef(Entry.SpawnPointId);
			if (!Entry.BoarClass || Entry.BoarUniqueId.IsNone() || IdCounts.FindRef(Entry.BoarUniqueId) != 1
				|| !Point || DuplicatePoints.Contains(Entry.SpawnPointId))
			{
				UE_LOG(LogTemp, Error, TEXT("[Stage] Invalid/duplicate identity or spawn point: %s / %s"),
					*Entry.BoarUniqueId.ToString(), *Entry.SpawnPointId.ToString());
				bSucceeded = false;
				continue;
			}
			// BeginPlayより前にIDを渡し、生成順から永続IDを作らないようにします。
			ABoarBase* Boar = World->SpawnActorDeferred<ABoarBase>(Entry.BoarClass, Point->GetActorTransform(),
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (Boar)
			{
				Boar->BoarUniqueId = Entry.BoarUniqueId;
				UGameplayStatics::FinishSpawningActor(Boar, Point->GetActorTransform());
			}
			bSucceeded &= IsValid(Boar);
		}
		return bSucceeded;
	}

	if (SpawnPoints.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Stage] %s has BoarSpawnEntries but no BoarSpawnPoint actors."),
			*GetNameSafe(StageConfig));
		return false;
	}

	const int32 TotalSpawnCount = StageConfig->GetTotalBoarSpawnCount();
	if (StageConfig->TargetCaptureCount > TotalSpawnCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Stage] TargetCaptureCount (%d) exceeds configured Boar spawn count (%d)."),
			StageConfig->TargetCaptureCount,
			TotalSpawnCount);
	}

	int32 SpawnPointIndex = 0;
	int32 SpawnedCount = 0;
	for (const FBoarSpawnEntry& Entry : StageConfig->BoarSpawnEntries)
	{
		if (!Entry.BoarClass || Entry.Count <= 0)
		{
			continue;
		}

		for (int32 CountIndex = 0; CountIndex < Entry.Count; ++CountIndex)
		{
			ABoarSpawnPoint* SpawnPoint = SpawnPoints[SpawnPointIndex % SpawnPoints.Num()];
			++SpawnPointIndex;

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			if (World->SpawnActor<ABoarBase>(Entry.BoarClass, SpawnPoint->GetActorTransform(), SpawnParameters))
			{
				++SpawnedCount;
			}
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Stage] Spawned %d of %d configured Boars using %d spawn points."),
		SpawnedCount,
		TotalSpawnCount,
		SpawnPoints.Num());
	return SpawnedCount == TotalSpawnCount;
}

bool BoarStageSpawner::SpawnConfiguredSpecialCoins(UWorld* World, const UStageConfig* StageConfig, TSubclassOf<ASpecialCoin> SpecialCoinClass, const UBoarSaveGame* Progress)
{
	if (!StageConfig || StageConfig->SpecialCoinDefinitions.IsEmpty()) return true;
	if (!SpecialCoinClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Stage] SpecialCoinDefinitions require SpecialCoinClass."));
		return false;
	}
	if (!World) return false;
	bool bSucceeded = true;
	TMap<FName, ABoarSpawnPoint*> Points;
	TSet<FName> DuplicatePoints;
	for (TActorIterator<ABoarSpawnPoint> It(World); It; ++It)
	{
		if (It->SpawnPointId.IsNone()) continue;
		if (Points.Contains(It->SpawnPointId)) DuplicatePoints.Add(It->SpawnPointId);
		Points.Add(It->SpawnPointId, *It);
	}
	TMap<FName, int32> IdCounts;
	for (const FSpecialCoinDefinition& Entry : StageConfig->SpecialCoinDefinitions) ++IdCounts.FindOrAdd(Entry.SpecialCoinId);
	for (TActorIterator<ASpecialCoin> It(World); It; ++It)
		if (!It->SpecialCoinId.IsNone()) ++IdCounts.FindOrAdd(It->SpecialCoinId);
	for (const FSpecialCoinDefinition& Entry : StageConfig->SpecialCoinDefinitions)
	{
		ABoarSpawnPoint* Point = Points.FindRef(Entry.SpawnPointId);
		if (Entry.SpecialCoinId.IsNone() || IdCounts.FindRef(Entry.SpecialCoinId) != 1
			|| !Point || DuplicatePoints.Contains(Entry.SpawnPointId))
		{
			UE_LOG(LogTemp, Error, TEXT("[Stage] Invalid/duplicate coin or spawn point: %s / %s"),
				*Entry.SpecialCoinId.ToString(), *Entry.SpawnPointId.ToString());
			bSucceeded = false;
			continue;
		}
		if (Progress && Progress->SpecialCoinIds.Contains(Entry.SpecialCoinId)) continue;
		ASpecialCoin* Coin = World->SpawnActorDeferred<ASpecialCoin>(SpecialCoinClass, Point->GetActorTransform());
		if (Coin)
		{
			Coin->SpecialCoinId = Entry.SpecialCoinId;
			UGameplayStatics::FinishSpawningActor(Coin, Point->GetActorTransform());
		}
		bSucceeded &= IsValid(Coin);
	}
	return bSucceeded;
}
