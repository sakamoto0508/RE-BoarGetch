// Fill out your copyright notice in the Description page of Project Settings.

#include "Gadget/NetGadget.h"

#include "Boar/BoarBase.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

// Sets default values
ANetGadget::ANetGadget()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ANetGadget::Use_Implementation(AActor* TargetActor)
{
	if (!CanUse())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Net] Use blocked by cooldown"));
		return;
	}

	//ネットの位置から範囲内のイノシシを探す。
	FVector NetLocation = GetActorLocation();
	CaptureBoarInRange(NetLocation);

	StartCooldown();
}

void ANetGadget::CaptureBoarInRange(const FVector& CenterLocation)
{
	UWorld* World = GetWorld();
	if (World == nullptr) return;
	
	/// <summary>
	/// SphereTrace で範囲内のアクターを全部探します。
	/// </summary>
	FCollisionShape sphere = FCollisionShape::MakeSphere(CaptureRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());
	
	// 球体内で重なっているActorの情報を格納する配列
	TArray<FOverlapResult> OverlapResults;
	
	// 球体判定を実行する
    // CenterLocation を中心、sphere を半径とした球の中にある Pawn を検索する
    // 見つかった結果は OverlapResults に格納される
	World->OverlapMultiByChannel(OverlapResults,CenterLocation,
		FQuat::Identity,ECC_Pawn,sphere,QueryParams);

	int32 CapturedCount = 0;
	for (const FOverlapResult& Result:OverlapResults)
	{
		if (ABoarBase* Boar=Cast<ABoarBase>(Result.GetActor()))
		{
			Boar->Capture();
			++CapturedCount;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Net] Overlap=%d Captured=%d Radius=%.1f"),
		OverlapResults.Num(), CapturedCount, CaptureRadius);
}
