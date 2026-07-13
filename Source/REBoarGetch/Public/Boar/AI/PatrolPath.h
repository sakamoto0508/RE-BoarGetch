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

	/// <summary>
	/// 指定したIndexの巡回ポイントを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category="Patrol")
	FVector GetPatrolPoint(int32 Index) const;

	/// <summary>
	/// 巡回ポイント数を返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category="Patrol")
	int32 GetPointCount() const;

private:

	/// <summary>
	/// ルートです。
	/// </summary>
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/// <summary>
	/// 巡回ポイントです。
	/// </summary>
	UPROPERTY(EditInstanceOnly, Category="Patrol")
	TArray<TObjectPtr<USceneComponent>> PatrolPoints;
};