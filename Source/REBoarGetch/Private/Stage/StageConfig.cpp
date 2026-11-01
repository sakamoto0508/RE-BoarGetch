#include "Stage/StageConfig.h"

#include "Boar/BoarBase.h"

int32 UStageConfig::GetTotalBoarSpawnCount() const
{
	int32 TotalCount = 0;
	// Class・個体ID・地点IDが設定された要素だけ数えます。
	// 実際の生成可否（ID重複・Level内の地点）はGameModeの生成処理で検証します。
	for (const FBoarSpawnDefinition& Entry : BoarSpawnDefinitions)
		if (Entry.BoarClass && !Entry.BoarUniqueId.IsNone() && !Entry.SpawnPointId.IsNone()) 
			++TotalCount;
	return TotalCount;
}
