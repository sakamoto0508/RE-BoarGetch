#include "Stage/StageConfig.h"

#include "Boar/BoarBase.h"

int32 UStageConfig::GetTotalBoarSpawnCount() const
{
	int32 TotalCount = 0;
	// 個体定義があれば旧方式と合算せず、Class・個体ID・地点IDが設定された要素だけ数えます。
	// 実際の生成可否（ID重複・Level内の地点）はGameModeの生成処理で検証します。
	if (!BoarSpawnDefinitions.IsEmpty())
	{
		for (const FBoarSpawnDefinition& Entry : BoarSpawnDefinitions)
			if (Entry.BoarClass && !Entry.BoarUniqueId.IsNone() && !Entry.SpawnPointId.IsNone()) ++TotalCount;
		return TotalCount;
	}
	// 個体定義のない既存アセットは、互換用のClass・体数設定から集計します。
	for (const FBoarSpawnEntry& Entry : BoarSpawnEntries)
	{
		if (Entry.BoarClass && Entry.Count > 0)
		{
			TotalCount += Entry.Count;
		}
	}
	return TotalCount;
}
