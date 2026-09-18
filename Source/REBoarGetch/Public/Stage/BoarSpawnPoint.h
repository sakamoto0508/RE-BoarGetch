#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoarSpawnPoint.generated.h"

class USceneComponent;

/**
 * @brief StageConfigで指定されたBoarの生成位置を表すLevel配置用Actorです。
 *
 * BoarGameModeはStage開始時にLevel内のSpawnPointを収集し、設定されたBoarを
 * 配置順に循環させながら生成します。位置と向きはActor Transformを使用します。
 */
UCLASS(Blueprintable)
class REBOARGETCH_API ABoarSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	/** @brief Boar生成位置として使用するScene Rootを構築します。 */
	ABoarSpawnPoint();

private:
	/** Spawn位置と向きの基点です。 */
	UPROPERTY(VisibleAnywhere, Category = "REBoarGetch|Stage|Spawn")
	TObjectPtr<USceneComponent> SceneRoot;
};
