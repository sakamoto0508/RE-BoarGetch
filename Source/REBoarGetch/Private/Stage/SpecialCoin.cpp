#include "Stage/SpecialCoin.h"
#include "Components/SceneComponent.h"
#include "BoarGameInstance.h"
#include "BoarSaveGame.h"
#include "Core/BoarGameMode.h"
#include "Player/BoarPlayerCharacter.h"
#include "Engine/World.h"

ASpecialCoin::ASpecialCoin()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("CoinRoot")));
}

void ASpecialCoin::BeginPlay()
{
	Super::BeginPlay();
	const UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>();
	const UBoarSaveGame* Save = Instance ? Instance->GetProgress() : nullptr;
	if (Save && Save->SpecialCoinIds.Contains(SpecialCoinId))
	{
		bCollected = true;
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}
}

bool ASpecialCoin::TryCollect(AActor* Collector)
{
	ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>();
	if (bCollected || !Cast<ABoarPlayerCharacter>(Collector) || !Mode || !Mode->RecordSpecialCoin(SpecialCoinId)) return false;
	bCollected = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	OnCoinCollected();
	return true;
}
