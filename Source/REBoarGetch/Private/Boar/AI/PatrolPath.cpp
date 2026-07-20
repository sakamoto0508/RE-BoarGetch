#include "Boar/AI/PatrolPath.h"

#include "Components/SceneComponent.h"

APatrolPath::APatrolPath()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

// 不正なIndexならActor自身の位置を返す。
// クラッシュ防止用。
FVector APatrolPath::GetPatrolPoint(int32 Index) const
{
	if (!PatrolPoints.IsValidIndex(Index)) return GetActorLocation();
	
	return PatrolPoints[Index]->GetComponentLocation();
}

int32 APatrolPath::GetPointCount() const
{
	return PatrolPoints.Num();
}