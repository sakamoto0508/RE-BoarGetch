// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BoarGameMode.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"
#include "Item/HealPickup.h"
#include "Player/BoarPlayerController.h"
#include "Player/BoarPlayerCharacter.h"
#include "EngineUtils.h"

ABoarGameMode::ABoarGameMode()
{
	// プレイヤーコントローラーを指定
	PlayerControllerClass = ABoarPlayerController::StaticClass();

	// デフォルトポーンクラスを指定
	DefaultPawnClass = ABoarPlayerCharacter::StaticClass();
}

void ABoarGameMode::HandleBoarCaptured(ABoarBase* Boar)
{
	if (Boar == nullptr)	return;
	
	if (UWorld* World = GetWorld())
	{
		// 先頭で見つかった檻を収容先として使う。
		for (TActorIterator<ACage> It(World); It; ++It)
		{
			if (ACage* Cage = *It)
			{
				Cage->CollectBoar(Boar);
				break;
			}
		}

		++CapturedBoarCount;

		// 捕獲地点に回復ピックアップを確率ドロップする。
		if (HealPickupClass && FMath::FRand() <= HealItemDropChance)
		{
			const FVector SpawnLocation = Boar->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
			World->SpawnActor<AHealPickup>(HealPickupClass, SpawnLocation, FRotator::ZeroRotator);
		}
	}
}

void ABoarGameMode::HandlePlayerDeath(ABoarPlayerCharacter* PlayerCharacter)
{
	(void)PlayerCharacter;
	OnGameOver();
}