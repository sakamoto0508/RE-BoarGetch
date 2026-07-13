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

	/// <summary>実行中のイノシシです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Context")
	TObjectPtr<APawn> NPC;

	/// <summary>実行中のAIControllerです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Context")
	TObjectPtr<AAIController> Controller;

	/// <summary>認識中プレイヤーです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	TObjectPtr<APawn> TargetPlayer;

	/// <summary>認識中の檻です。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	TObjectPtr<ACage> TargetCage;

	/// <summary>認識中プレイヤーが存在するかです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bHasTargetPlayer = false;

	/// <summary>認識中の檻が存在するかです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bHasTargetCage = false;

	/// <summary>視界内に対象が存在するかです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bHasTargetInSight = false;

	/// <summary>檻を優先するかです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bPreferCage = false;

	/// <summary>逃走を優先するかです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bPreferEscape = false;

	/// <summary>檻を攻撃できるかです。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	bool bCanAttackCage = true;

	/// <summary>檻までの距離です。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	float DistanceToCage = BIG_NUMBER;

	/// <summary>巡回地点です。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	FVector PatrolLocation = FVector::ZeroVector;

	/// <summary>現在の巡回ポイント番号です。</summary>
	UPROPERTY(VisibleAnywhere, Category = "Output")
	int32 CurrentPatrolIndex = 0;

	/// <summary> プレイヤーから逃げる場所。</summary>
	UPROPERTY(VisibleAnywhere, Category="Output")
	FVector EscapeLocation = FVector::ZeroVector;
};
