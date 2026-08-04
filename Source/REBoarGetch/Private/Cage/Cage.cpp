// Fill out your copyright notice in the Description page of Project Settings.


#include "Cage/Cage.h"
#include "Boar/BoarBase.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Component/CaptureComponent.h"

// Sets default values
ACage::ACage()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CapturedBoarArea = CreateDefaultSubobject<UBoxComponent>(TEXT("CapturedBoarArea"));
	CapturedBoarArea->SetupAttachment(SceneRoot);
	CapturedBoarArea->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	CapturedBoarArea->SetBoxExtent(FVector(200.0f, 200.0f, 100.0f));
	CapturedBoarArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapturedBoarArea->SetGenerateOverlapEvents(false);
}

void ACage::BeginPlay()
{
	Super::BeginPlay();

	MaxHp = FMath::Max(MaxHp, 1.0f);
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

	// 破棄済みの参照を除外し、新しい個体が空いた収容枠を利用できるようにする。
	CapturedBoars.RemoveAll([](const TObjectPtr<ABoarBase>& CapturedBoar)
	{
		return !IsValid(CapturedBoar);
	});

	// AddUniqueは同じイノシシが既に配列に存在する場合、重複して追加せず、既存要素のインデックスを返す。
	const int32 SlotIndex = CapturedBoars.AddUnique(Boar);
	
	// 配列上の位置に対応する収容座標へイノシシを移動させる。
	// TeleportPhysicsを指定することで、物理速度による移動ではなく 指定した座標へ即座に配置する。
	Boar->SetActorLocation(
		GetCapturedBoarLocation(SlotIndex, Boar),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	UE_LOG(LogTemp, Log, TEXT("[Cage] Collected %s Total=%d"), *GetNameSafe(Boar), CapturedBoars.Num());
}

FVector ACage::GetCapturedBoarLocation(int32 SlotIndex, const ABoarBase* Boar) const
{
	if (CapturedBoarArea == nullptr)
		return GetActorLocation();

	const int32 BoarsPerRow = FMath::Max(1, CapturedBoarsPerRow);
	FVector2D SlotOffset = CapturedBoarCenterOffset;

	// 1頭目は中央へ置き、2頭目以降は中央の前後へ交互に列を増やす。
	if (SlotIndex > 0)
	{
		const int32 GridIndex = SlotIndex - 1;
		const int32 Row = GridIndex / BoarsPerRow + 1;
		const int32 Column = GridIndex % BoarsPerRow;
		const float CenteredColumn = static_cast<float>(Column) - static_cast<float>(BoarsPerRow - 1) * 0.5f;
		const int32 SignedRow = (Row % 2 == 1) ? (Row + 1) / 2 : -(Row / 2);

		SlotOffset.X += CenteredColumn * CapturedBoarSpacing;
		SlotOffset.Y += static_cast<float>(SignedRow) * CapturedBoarSpacing;
	}

	const FVector AreaExtent = CapturedBoarArea->GetScaledBoxExtent();
	const float CapsuleHalfHeight = Boar && Boar->GetCapsuleComponent()
		? Boar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 0.0f;

	return CapturedBoarArea->GetComponentLocation()
		+ CapturedBoarArea->GetForwardVector() * SlotOffset.X
		+ CapturedBoarArea->GetRightVector() * SlotOffset.Y
		+ CapturedBoarArea->GetUpVector()
			* (-AreaExtent.Z + CapsuleHalfHeight + CapturedBoarFloorOffset);
}

//檻へのダメージ。イノシシが呼び出す関数。
void ACage::ApplyDamage(float Damage)
{
	if (bIsDestroyed || Damage <= 0.0f)
		return;

	CurrentHp = FMath::Clamp(CurrentHp - Damage, 0.0f, MaxHp);
	OnHealthChanged.Broadcast(CurrentHp, MaxHp);
	
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

	OnCageDestroyed.Broadcast();
	
	// Actor自体はDestroyせず、非表示にして当たり判定を停止する。
	// Destroy()すると、このActor自身から復活処理を呼べなくなる。
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);

	// 捕まっているイノシシを解放する処理
	for (ABoarBase* Boar : CapturedBoars)
	{
		if (IsValid(Boar))
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

	OnHealthChanged.Broadcast(CurrentHp, MaxHp);
	OnRespawned.Broadcast();
	
	//まず、見た目を復活。
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	// 実行済みタイマーのハンドルを明示的にクリアする。
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
}
