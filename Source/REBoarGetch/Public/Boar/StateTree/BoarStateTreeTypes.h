#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoarStateTreeTypes.generated.h"

class AAIController;
class ACage;
class APawn;

USTRUCT()
struct FBoarStateTreeInstanceData
{
	GENERATED_BODY()

	/** 実行中のイノシシです。 */
	UPROPERTY(VisibleAnywhere, Category="Context")
	TObjectPtr<APawn> NPC;

	/** 実行中のAIControllerです。 */
	UPROPERTY(VisibleAnywhere, Category="Context")
	TObjectPtr<AAIController> Controller;

	/** 認識中プレイヤーです。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	TObjectPtr<APawn> TargetPlayer;

	/** 認識中の檻です。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	TObjectPtr<ACage> TargetCage;

	/** 認識中プレイヤーが存在するかです。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bHasTargetPlayer = false;

	/** 認識中の檻が存在するかです。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bHasTargetCage = false;

	/** 視界内に対象が存在するかです。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bHasTargetInSight = false;

	/** 檻を優先するかです。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bPreferCage = false;

	/** 逃走を優先するかです。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bPreferEscape = false;

	/** 檻を攻撃できるかです。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bCanAttackCage = true;

	/** 檻までの距離です。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	float DistanceToCage = BIG_NUMBER;

	/** 巡回地点です。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	FVector PatrolLocation = FVector::ZeroVector;

	/** 現在の巡回ポイント番号です。 */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	int32 CurrentPatrolIndex = 0;

	/** プレイヤーから逃げる場所。 */
	UPROPERTY(VisibleAnywhere, Category="Output")
	FVector EscapeLocation = FVector::ZeroVector;
};
