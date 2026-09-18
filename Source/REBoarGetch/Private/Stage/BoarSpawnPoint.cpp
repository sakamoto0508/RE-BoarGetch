#include "Stage/BoarSpawnPoint.h"

#include "Components/SceneComponent.h"

ABoarSpawnPoint::ABoarSpawnPoint()
{
	// 位置情報だけを提供するActorなので、毎Frameの更新は不要です。
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}
