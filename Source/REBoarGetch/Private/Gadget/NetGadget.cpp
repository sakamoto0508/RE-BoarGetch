// Fill out your copyright notice in the Description page of Project Settings.

#include "Gadget/NetGadget.h"

#include "Boar/BoarBase.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

ANetGadget::ANetGadget()
{
	PrimaryActorTick.bCanEverTick = false;
	EquipSocketName = TEXT("NetSocket");
	// ネットは単発ガジェットとして扱う。
	UseStyle = EGadgetUseStyle::OneShot;
	// 新しいルートコンポーネント（シーンコンポーネント）を作成して設定。
	USceneComponent* DummyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DummyRoot"));
	SetRootComponent(DummyRoot);
	// スフィアコリジョンを作成。
	CaptureCollision = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureCollision"));
	// スフィアコリジョンをルート（DummyRoot）の子階層として繋ぐ。
	CaptureCollision->SetupAttachment(DummyRoot);
	// 初期状態ではセンサーを「オフ」にする。
	CaptureCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// センサーの「属性」を決める。
	CaptureCollision->SetCollisionObjectType(ECC_WorldDynamic);
	// PawnのみOverlapする。
	CaptureCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CaptureCollision->SetGenerateOverlapEvents(true);
	// Overlap開始時のイベントを登録。
	CaptureCollision->OnComponentBeginOverlap.AddDynamic(this, &ANetGadget::OnCaptureCollisionBeginOverlap);
}

// ガジェット使用。
void ANetGadget::Use_Implementation(AActor* TargetActor)
{
	if (!CanUse())	return;
	
	(void)TargetActor;

	EndCaptureWindow();
	AttemptedBoarsThisUse.Reset();
	bSuccessFeedbackPlayedThisUse = false;

	StartCooldown();
}

// 捕獲判定を有効化。
void ANetGadget::BeginCaptureWindow()
{
	if (CaptureCollision == nullptr)	return;
	// SphereCollisionを有効化。
	CaptureCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	bCaptureWindowActive = true;
	// Sweepは判定窓を開いた位置から始める。
	LastCaptureSweepLocation = CaptureCollision->GetComponentLocation();
	bHasCaptureSweepLocation = true;
	// 既に範囲内にいるイノシシも判定する。
	CaptureOverlappingBoars();
}

// 捕獲判定を終了。
void ANetGadget::EndCaptureWindow()
{
	if (CaptureCollision)
	{
		CaptureCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	bCaptureWindowActive = false;
	bHasCaptureSweepLocation = false;
}

// 前回位置から現在位置までSphereを掃引し、通り抜けたイノシシを捕獲する。
void ANetGadget::SweepCaptureWindow()
{
	if (!bCaptureWindowActive || CaptureCollision == nullptr) return;
	UWorld* World = GetWorld();
	if (World == nullptr) return;

	const FVector Current = CaptureCollision->GetComponentLocation();
	const FVector Start = bHasCaptureSweepLocation ? LastCaptureSweepLocation : Current;
	LastCaptureSweepLocation = Current;
	bHasCaptureSweepLocation = true;
	// 移動がほぼ無い場合は通常のOverlap判定で足りる。
	if (FVector::DistSquared(Start, Current) < 1.0f) return;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NetCaptureSweep), false, this);
	Params.AddIgnoredActor(GetOwner());
	TArray<FHitResult> Hits;
	// 通常判定と同じくPawnのみを対象にし、半径もSphereCollisionの実寸を使う。
	World->SweepMultiByObjectType(Hits, Start, Current, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(CaptureCollision->GetScaledSphereRadius()), Params);
	for (const FHitResult& Hit : Hits)
	{
		TryCaptureBoar(Cast<ABoarBase>(Hit.GetActor()));
	}
}

// SphereCollisionと重なったときに呼ばれる。
void ANetGadget::OnCaptureCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;
	
	if (!bCaptureWindowActive)	return;

	TryCaptureBoar(Cast<ABoarBase>(OtherActor));
}

// イノシシの捕獲を試みる
bool ANetGadget::TryCaptureBoar(ABoarBase* Boar)
{
	if (!bCaptureWindowActive || Boar == nullptr || Boar->IsCaptured() || AttemptedBoarsThisUse.Contains(Boar)) return false;
	AttemptedBoarsThisUse.Add(Boar);
	
	if (!Boar->CaptureWithFeedback(!bSuccessFeedbackPlayedThisUse)) return false;
	bSuccessFeedbackPlayedThisUse = true;
	UE_LOG(LogTemp, Log, TEXT("[Net] Captured %s"), *GetNameSafe(Boar));
	return true;
}

// Sphere内にいるイノシシを捕獲する
void ANetGadget::CaptureOverlappingBoars()
{
	if (CaptureCollision == nullptr || !bCaptureWindowActive)	return;
	
	TArray<AActor*> OverlappingActors;
	// Sphere内のイノシシを取得
	CaptureCollision->GetOverlappingActors(OverlappingActors, ABoarBase::StaticClass());

	for (AActor* OverlapActor : OverlappingActors)
	{
		TryCaptureBoar(Cast<ABoarBase>(OverlapActor));
	}
}
