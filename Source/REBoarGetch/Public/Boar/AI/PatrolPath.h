#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PatrolPath.generated.h"

class USceneComponent;

UCLASS()
class REBOARGETCH_API APatrolPath : public AActor
{
	GENERATED_BODY()

public:
	APatrolPath();

	/**
	 * 指定したIndexの巡回ポイントを返します。
	 */
	UFUNCTION(BlueprintPure, Category="Patrol")
	FVector GetPatrolPoint(int32 Index) const;

	/**
	 * 巡回ポイント数を返します。
	 */
	UFUNCTION(BlueprintPure, Category="Patrol")
	int32 GetPointCount() const;

private:

	/**
	 * ルートです。
	 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/**
	 * 巡回ポイントです。
	 */
	UPROPERTY(EditInstanceOnly, Category="Patrol")
	TArray<TObjectPtr<USceneComponent>> PatrolPoints;
};
