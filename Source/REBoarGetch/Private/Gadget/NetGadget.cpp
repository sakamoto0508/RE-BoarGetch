// Fill out your copyright notice in the Description page of Project Settings.

#include "Gadget/NetGadget.h"

#include "Boar/BoarBase.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"

ANetGadget::ANetGadget()
{
	PrimaryActorTick.bCanEverTick = false;
	// ネットは単発ガジェットとして扱う。
	UseStyle = EGadgetUseStyle::OneShot;

	CaptureCollision = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureCollision"));
	SetRootComponent(CaptureCollision);
	// 初期設定。
	CaptureCollision->SetSphereRadius(CaptureRadius);
	CaptureCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CaptureCollision->SetCollisionObjectType(ECC_WorldDynamic);
	// PawnとのみOverlapする。
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

	StartCooldown();
}

// 捕獲判定を有効化。
void ANetGadget::BeginCaptureWindow()
{
	if (CaptureCollision == nullptr)	return;
	// SphereCollisionを有効化。
	CaptureCollision->SetSphereRadius(CaptureRadius);
	CaptureCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	bCaptureWindowActive = true;
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
	if (!bCaptureWindowActive || Boar == nullptr || Boar->IsCaptured())	return false;
	
	Boar->Capture();
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
