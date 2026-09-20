#include "Stage/StageConfig.h"

#include "Boar/BoarBase.h"

int32 UStageConfig::GetTotalBoarSpawnCount() const
{
	int32 TotalCount = 0;
	if (!BoarSpawnDefinitions.IsEmpty())
	{
		for (const FBoarSpawnDefinition& Entry : BoarSpawnDefinitions)
			if (Entry.BoarClass && !Entry.BoarUniqueId.IsNone() && !Entry.SpawnPointId.IsNone()) ++TotalCount;
		return TotalCount;
	}
	for (const FBoarSpawnEntry& Entry : BoarSpawnEntries)
	{
		if (Entry.BoarClass && Entry.Count > 0)
		{
			TotalCount += Entry.Count;
		}
	}
	return TotalCount;
}
