// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BoarGameMode.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"
#include "Item/HealPickup.h"
#include "Player/BoarPlayerController.h"
#include "Player/BoarPlayerCharacter.h"
#include "Stage/StageConfig.h"
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
		const int32 TargetCaptureCount = StageConfig ? StageConfig->TargetCaptureCount : 0;
		OnCapturedBoarCountChanged.Broadcast(CapturedBoarCount, TargetCaptureCount);

		// 捕獲地点に回復ピックアップを確率ドロップする。
		if (HealPickupClass && FMath::FRand() <= HealItemDropChance)
		{
			const FVector SpawnLocation = Boar->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
			World->SpawnActor<AHealPickup>(HealPickupClass, SpawnLocation, FRotator::ZeroRotator);
		}

		// 捕獲に伴う既存の後処理を終えてから、画面遷移を起こし得るクリアイベントを通知します。
		EvaluateStageClearCondition();
	}
}

void ABoarGameMode::EvaluateStageClearCondition()
{
	// 通知済み、またはステージ設定未登録の場合はクリア判定を行わない。
	if (bStageClearRequested || StageConfig == nullptr)
	{
		return;
	}

	const int32 TargetCaptureCount = StageConfig->TargetCaptureCount;
	// 目標値0以下は判定無効。正数の場合のみ到達数と比較する。
	if (TargetCaptureCount <= 0 || CapturedBoarCount < TargetCaptureCount)
	{
		return;
	}

	// Blueprintイベント内で画面遷移が始まる可能性があるため、通知前に多重実行を禁止する。
	bStageClearRequested = true;

	if (ABoarPlayerController* PlayerController = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PlayerController->HandleStageCleared(CapturedBoarCount, TargetCaptureCount);
	}

	// Level固有の演出やSEを追加できるよう、Blueprintイベントも引き続き通知する。
	OnStageCleared();
}

void ABoarGameMode::HandlePlayerDeath(ABoarPlayerCharacter* PlayerCharacter)
{
	(void)PlayerCharacter;
	OnGameOver();
}
