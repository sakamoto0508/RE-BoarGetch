#pragma once

#include "CoreMinimal.h"

class UWorld;
class UStageConfig;
class UBoarSaveGame;
class ASpecialCoin;

/**
 * StageConfigに従ってBoar・特別コインをLevel内のBoarSpawnPointへ生成します。
 * GameModeは「いつ生成するか・成功したか」だけを判断し、生成方法（ID検証、SpawnActorDeferredでBeginPlay前に永続IDを設定、
 * 取得済みコインの除外）はここに置きます。初期化（Stage開始状態への設定）は各Actor自身のAPIが担当します。
 */
namespace BoarStageSpawner
{
/** BoarSpawnDefinitions（永続ID付き）または旧BoarSpawnEntriesから生成します。設定が空なら手置きBoarのみを使いtrueを返します。 */
bool SpawnConfiguredBoars(UWorld* World, const UStageConfig* StageConfig);
/** SpecialCoinDefinitionsから未取得のコインだけを生成します。定義が空ならtrueを返します。 */
bool SpawnConfiguredSpecialCoins(UWorld* World, const UStageConfig* StageConfig, TSubclassOf<ASpecialCoin> SpecialCoinClass, const UBoarSaveGame* Progress);
}
