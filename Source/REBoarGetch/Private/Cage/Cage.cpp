// Fill out your copyright notice in the Description page of Project Settings.


#include "Cage/Cage.h"
#include "Boar/BoarBase.h"
#include "AIController.h"
#include "TimerManager.h"
#include "Component/CaptureComponent.h"

// Sets default values
ACage::ACage()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACage::BeginPlay()
{
	Super::BeginPlay();

	CurrentHp = MaxHp;
	bIsDestroyed = false;
}

void ACage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// レベル移動やActor削除時に、残っているタイマーを停止する。
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);

	Super::EndPlay(EndPlayReason);
}

//捕獲したイノシシを檻に追加する。檻の位置にイノシシを移動させる。
void ACage::CollectBoar(ABoarBase* Boar)
{
	if (Boar == nullptr) return;
	if (bIsDestroyed) return;

	//配列に同じ要素が存在しない場合だけ追加する関数。
	CapturedBoars.AddUnique(Boar);
	Boar->SetActorLocation(GetActorLocation());
	UE_LOG(LogTemp, Log, TEXT("[Cage] Collected %s Total=%d"), *GetNameSafe(Boar), CapturedBoars.Num());
}

//檻へのダメージ。イノシシが呼び出す関数。
void ACage::ApplyDamage(float Damage)
{
	if (bIsDestroyed || Damage <= 0.0f)
		return;

	CurrentHp = FMath::Clamp(CurrentHp - Damage, 0.0f, MaxHp);

	if (CurrentHp <= 0.0f)
	{
		DestroyCage();
	}
}

//檻が破壊された時に呼ばれる処理。破壊された檻に捕まっていたイノシシを解放する。
void ACage::DestroyCage()
{
	if (bIsDestroyed) return;
	bIsDestroyed = true;

	// Actor自体はDestroyせず、非表示にして当たり判定を停止する。
	// Destroy()すると、このActor自身から復活処理を呼べなくなる。
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);

	// 捕まっているイノシシを解放する処理
	for (ABoarBase* Boar : CapturedBoars)
	{
		if (Boar)
		{
			Boar->ReleaseBoar();
		}
	}
	CapturedBoars.Empty();

	// RespawnDelay秒後にRespawnCageを1回だけ呼び出す。
	// このタイマーはゲーム時間を基準に進むため、
	// 通常のポーズ中は停止し、スローモーション中は遅く進む。
	if (RespawnDelay <= 0.0f)
	{
		RespawnCage();
		return;
	}

	// 再出現タイマーを開始
	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ACage::RespawnCage, RespawnDelay, false);
}

//檻が再出現する処理。一定時間後に呼ばれる。
void ACage::RespawnCage()
{
	if (!bIsDestroyed) return;

	CurrentHp = MaxHp;
	bIsDestroyed = false;

	//まず、見た目を復活。
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	// 実行済みタイマーのハンドルを明示的にクリアする。
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
}
