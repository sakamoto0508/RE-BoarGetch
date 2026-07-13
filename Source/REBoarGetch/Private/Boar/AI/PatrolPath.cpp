#include "Boar/AI/PatrolPath.h"

#include "Components/SceneComponent.h"

APatrolPath::APatrolPath()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

FVector APatrolPath::GetPatrolPoint(int32 Index) const
{
	if (!PatrolPoints.IsValidIndex(Index)) return GetActorLocation();
	
	return PatrolPoints[Index]->GetComponentLocation();
}

int32 APatrolPath::GetPointCount() const
{
	return PatrolPoints.Num();
}