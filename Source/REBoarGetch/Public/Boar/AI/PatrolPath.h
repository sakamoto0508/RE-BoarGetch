#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PatrolPath.generated.h"

class USceneComponent;

/** レベル上に配置した複数の巡回地点をイノシシAIへ提供するActorです。 */
UCLASS()
class REBOARGETCH_API APatrolPath : public AActor
{
	GENERATED_BODY()

public:
	/** ルートComponentと巡回地点管理用の初期状態を構築します。 */
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
