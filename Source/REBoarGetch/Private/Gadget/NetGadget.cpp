#include "Gadget/NetGadget.h"
#include "REBoarGetch.h"

#include "Boar/BoarBase.h"
#include "Player/BoarPlayerCharacter.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

ANetGadget::ANetGadget()
{
	PrimaryActorTick.bCanEverTick = false;
	EquipSocketName = TEXT("NetSocket");

	UseStyle = EGadgetUseStyle::OneShot;

	USceneComponent* DummyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DummyRoot"));
	SetRootComponent(DummyRoot);

	CaptureCollision = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureCollision"));

	CaptureCollision->SetupAttachment(DummyRoot);

	// 装備しただけで捕獲が成立しないよう、判定窓が開くまで衝突を無効にする。
	CaptureCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CaptureCollision->SetCollisionObjectType(ECC_WorldDynamic);

	CaptureCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CaptureCollision->SetGenerateOverlapEvents(true);

	CaptureCollision->OnComponentBeginOverlap.AddDynamic(this, &ANetGadget::OnCaptureCollisionBeginOverlap);
}

void ANetGadget::Use_Implementation(AActor* TargetActor)
{
	if (!CanUse())
		return;

	(void)TargetActor;

	EndCaptureWindow();
	AttemptedBoarsThisUse.Reset();
	bSuccessFeedbackPlayedThisUse = false;

	StartCooldown();
}

void ANetGadget::BeginCaptureWindow()
{
	if (CaptureCollision == nullptr)
		return;
	// Notify以外の経路からも、中断済み（Stun・Downed・装備解除・Stage終了）の使用で判定窓を開かない。
	// Playerが所有しない網（検証用の単体配置など）は従来どおり開ける。
	if (const ABoarPlayerCharacter* Player = Cast<ABoarPlayerCharacter>(GetOwner()))
		if (!Player->CanOpenNetCaptureWindow(this))
			return;
	CaptureCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	bCaptureWindowActive = true;

	// 判定窓が開く前に網が通った範囲をSweepに含めないよう、開始位置をここで記録する。
	LastCaptureSweepLocation = CaptureCollision->GetComponentLocation();
	bHasCaptureSweepLocation = true;

	// 衝突を有効化した瞬間のOverlap通知は判定窓のフラグ設定前に届き得るため、初回は明示的に調べる。
	CaptureOverlappingBoars();
}

void ANetGadget::EndCaptureWindow()
{
	if (CaptureCollision)
	{
		CaptureCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	bCaptureWindowActive = false;
	bHasCaptureSweepLocation = false;
}

// 網が1フレーム間に個体を通り抜けても、接触を取りこぼさないようにする。
void ANetGadget::SweepCaptureWindow()
{
	if (!bCaptureWindowActive || CaptureCollision == nullptr)
		return;

	UWorld* World = GetWorld();
	if (World == nullptr)
		return;

	const FVector Current = CaptureCollision->GetComponentLocation();
	const FVector Start = bHasCaptureSweepLocation ? LastCaptureSweepLocation : Current;
	LastCaptureSweepLocation = Current;
	bHasCaptureSweepLocation = true;

	// 移動がほぼ無い場合は通常のOverlap判定で足りる。
	if (FVector::DistSquared(Start, Current) < 1.0f)
		return;

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

void ANetGadget::OnCaptureCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	if (!bCaptureWindowActive)
		return;

	TryCaptureBoar(Cast<ABoarBase>(OtherActor));
}

bool ANetGadget::TryCaptureBoar(ABoarBase* Boar)
{
	if (!bCaptureWindowActive || Boar == nullptr || Boar->IsCaptured() || AttemptedBoarsThisUse.Contains(Boar))
		return false;

	// OverlapとSweepの両方から届いても同じ一振りで再試行しないよう、成否を受け取る前に記録する。
	AttemptedBoarsThisUse.Add(Boar);

	if (!Boar->CaptureWithFeedback(!bSuccessFeedbackPlayedThisUse))
		return false;

	bSuccessFeedbackPlayedThisUse = true;
	UE_LOG(LogREBoarGetch, Log, TEXT("[Net] Captured %s"), *GetNameSafe(Boar));
	return true;
}

void ANetGadget::CaptureOverlappingBoars()
{
	if (CaptureCollision == nullptr || !bCaptureWindowActive)
		return;

	TArray<AActor*> OverlappingActors;

	CaptureCollision->GetOverlappingActors(OverlappingActors, ABoarBase::StaticClass());

	for (AActor* OverlapActor : OverlappingActors)
	{
		TryCaptureBoar(Cast<ABoarBase>(OverlapActor));
	}
}
