#include "Item/HealPickup.h"

#include "Component/HealthComponent.h"
#include "Components/SphereComponent.h"
#include "Player/BoarPlayerCharacter.h"

AHealPickup::AHealPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetSphereRadius(80.0f);
	PickupCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupCollision->SetCollisionObjectType(ECC_WorldDynamic);
	PickupCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupCollision->SetGenerateOverlapEvents(true);
	// ピックアップはオーバーラップ開始で即時取得する。
	PickupCollision->OnComponentBeginOverlap.AddDynamic(this, &AHealPickup::OnPickupBeginOverlap);
}

void AHealPickup::OnPickupBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	ABoarPlayerCharacter* PlayerCharacter = Cast<ABoarPlayerCharacter>(OtherActor);
	if (PlayerCharacter == nullptr)	return;

	if (UHealthComponent* Health = PlayerCharacter->GetHealthComponent())
	{
		Health->Heal(HealAmount);
		// 二重取得を防ぐため、回復適用後に即Destroyする。
		Destroy();
	}
}
