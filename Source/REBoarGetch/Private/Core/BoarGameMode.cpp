// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BoarGameMode.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"
#include "Item/HealPickup.h"
#include "Player/BoarPlayerController.h"
#include "Player/BoarPlayerCharacter.h"
#include "Stage/StageConfig.h"
#include "Stage/BoarSpawnPoint.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "BoarGameInstance.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Component/GadgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gadget/GadgetBase.h"
#include "Kismet/GameplayStatics.h"
#include "Stage/SpecialCoin.h"
#include "BoarSaveGame.h"

ABoarGameMode::ABoarGameMode()
{
	// プレイヤーコントローラーを指定
	PlayerControllerClass = ABoarPlayerController::StaticClass();

	// デフォルトポーンクラスを指定
	DefaultPawnClass = ABoarPlayerCharacter::StaticClass();
}

void ABoarGameMode::HandleBoarCaptured(ABoarBase* Boar)
{
	if (!IsValid(Boar) || !Boar->IsCaptured() || !CanAdvanceStage() || HandledCapturedBoars.Contains(Boar)) return;
	HandledCapturedBoars.Add(Boar);
	const FVector CaptureLocation = Boar->GetActorLocation();
	ACage* NearestHealthy = nullptr;
	ACage* NearestDestroyed = nullptr;
	for (TActorIterator<ACage> It(GetWorld()); It; ++It)
	{
		ACage*& Candidate = It->GetIsCageDestroyed() ? NearestDestroyed : NearestHealthy;
		const double Distance = FVector::DistSquared(CaptureLocation, It->GetActorLocation());
		// 同距離だけは既存Actorのパス順で安定化します。永続IDには使用しません。
		if (!Candidate || Distance < FVector::DistSquared(CaptureLocation, Candidate->GetActorLocation())
			|| (Distance == FVector::DistSquared(CaptureLocation, Candidate->GetActorLocation())
				&& It->GetPathName() < Candidate->GetPathName())) Candidate = *It;
	}
	if (!Boar->BoarUniqueId.IsNone()) StageRunData.CapturedBoarUniqueIds.AddUnique(Boar->BoarUniqueId);
	else UE_LOG(LogTemp, Warning, TEXT("[Stage] %s has no persistent BoarUniqueId."), *GetNameSafe(Boar));
	if (ACage* Cage = NearestHealthy ? NearestHealthy : NearestDestroyed) Cage->CollectBoar(Boar);
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Stage] No cage exists; captured boar is released."));
		Boar->ReleaseBoar();
	}
	// 檻へ移動した後の座標ではなく、捕獲地点にドロップします。
	if (HealPickupClass && FMath::FRand() <= HealItemDropChance)
		GetWorld()->SpawnActor<AHealPickup>(HealPickupClass, CaptureLocation + FVector(0, 0, 40), FRotator::ZeroRotator);
	RefreshHousedBoarCount();
}

void ABoarGameMode::StartPlay()
{
	StageRunData = FStageRunData();
	StageRunData.StageId = StageConfig ? StageConfig->StageId : NAME_None;
	Super::StartPlay();
	// BeginPlayは通常のライフサイクルに任せ、Stage用の初期化順だけを明示します。
	SetStageActorsStopped(true);
	for (TActorIterator<ACage> It(GetWorld()); It; ++It) It->InitializeForStage();
	SpawnConfiguredBoars();
	UE_LOG(LogTemp, Log, TEXT("[StageInit] Boar spawning complete"));
	SpawnConfiguredSpecialCoins();
	for (TActorIterator<ABoarPlayerCharacter> It(GetWorld()); It; ++It) It->InitializeForStage();
	// 今生成したBoarと装備も含め、演出完了まで進行を停止します。
	SetStageActorsStopped(true);
	bStageInitializationComplete = true;
	UE_LOG(LogTemp, Log, TEXT("[StageInit] Preparing: initialization complete"));
	WorldTickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ABoarGameMode::HandleWorldTickEnd);
	if (bWaitForStageIntroduction && StageConfig)
	{
		OnStageIntroductionRequested();
	}
	else CompleteStageIntroduction();
}

void ABoarGameMode::SpawnConfiguredBoars()
{
	// Spawn Entryが空なら、既存Levelに手置きされたBoarだけを使用する。
	if (!StageConfig || (StageConfig->BoarSpawnEntries.IsEmpty() && StageConfig->BoarSpawnDefinitions.IsEmpty()))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<ABoarSpawnPoint*> SpawnPoints;
	for (TActorIterator<ABoarSpawnPoint> It(World); It; ++It)
	{
		SpawnPoints.Add(*It);
	}
	if (!StageConfig->BoarSpawnDefinitions.IsEmpty())
	{
		TMap<FName, ABoarSpawnPoint*> ById;
		TSet<FName> DuplicatePoints;
		for (ABoarSpawnPoint* Point : SpawnPoints)
		{
			if (Point->SpawnPointId.IsNone()) continue;
			if (ById.Contains(Point->SpawnPointId)) DuplicatePoints.Add(Point->SpawnPointId);
			ById.Add(Point->SpawnPointId, Point);
		}
		TMap<FName, int32> IdCounts;
		for (const FBoarSpawnDefinition& Entry : StageConfig->BoarSpawnDefinitions) ++IdCounts.FindOrAdd(Entry.BoarUniqueId);
		for (TActorIterator<ABoarBase> It(World); It; ++It)
			if (!It->BoarUniqueId.IsNone()) ++IdCounts.FindOrAdd(It->BoarUniqueId);
		for (const FBoarSpawnDefinition& Entry : StageConfig->BoarSpawnDefinitions)
		{
			ABoarSpawnPoint* Point = ById.FindRef(Entry.SpawnPointId);
			if (!Entry.BoarClass || Entry.BoarUniqueId.IsNone() || IdCounts.FindRef(Entry.BoarUniqueId) != 1
				|| !Point || DuplicatePoints.Contains(Entry.SpawnPointId))
			{
				UE_LOG(LogTemp, Error, TEXT("[Stage] Invalid/duplicate identity or spawn point: %s / %s"),
					*Entry.BoarUniqueId.ToString(), *Entry.SpawnPointId.ToString());
				continue;
			}
			// BeginPlayより前にIDを渡し、生成順から永続IDを作らないようにします。
			ABoarBase* Boar = World->SpawnActorDeferred<ABoarBase>(Entry.BoarClass, Point->GetActorTransform(),
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (Boar)
			{
				Boar->BoarUniqueId = Entry.BoarUniqueId;
				UGameplayStatics::FinishSpawningActor(Boar, Point->GetActorTransform());
			}
		}
		return;
	}

	if (SpawnPoints.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Stage] %s has BoarSpawnEntries but no BoarSpawnPoint actors."),
			*GetNameSafe(StageConfig));
		return;
	}

	const int32 TotalSpawnCount = StageConfig->GetTotalBoarSpawnCount();
	if (StageConfig->TargetCaptureCount > TotalSpawnCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Stage] TargetCaptureCount (%d) exceeds configured Boar spawn count (%d)."),
			StageConfig->TargetCaptureCount,
			TotalSpawnCount);
	}

	int32 SpawnPointIndex = 0;
	int32 SpawnedCount = 0;
	for (const FBoarSpawnEntry& Entry : StageConfig->BoarSpawnEntries)
	{
		if (!Entry.BoarClass || Entry.Count <= 0)
		{
			continue;
		}

		for (int32 CountIndex = 0; CountIndex < Entry.Count; ++CountIndex)
		{
			ABoarSpawnPoint* SpawnPoint = SpawnPoints[SpawnPointIndex % SpawnPoints.Num()];
			++SpawnPointIndex;

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			if (World->SpawnActor<ABoarBase>(Entry.BoarClass, SpawnPoint->GetActorTransform(), SpawnParameters))
			{
				++SpawnedCount;
			}
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Stage] Spawned %d of %d configured Boars using %d spawn points."),
		SpawnedCount,
		TotalSpawnCount,
		SpawnPoints.Num());
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

	RequestStageClear();
}

void ABoarGameMode::HandlePlayerDeath(ABoarPlayerCharacter* PlayerCharacter)
{
	(void)PlayerCharacter;
	RequestGameOver();
}

void ABoarGameMode::RefreshHousedBoarCount()
{
	if (!CanAdvanceStage()) return;
	int32 Count = 0;
	for (TActorIterator<ACage> It(GetWorld()); It; ++It) Count += It->GetCapturedBoarCount();
	for (auto It = HandledCapturedBoars.CreateIterator(); It; ++It)
		if (!It->IsValid() || !It->Get()->IsCaptured()) It.RemoveCurrent();
	CapturedBoarCount = Count;
	StageRunData.CurrentHousedCount = Count;
	OnCapturedBoarCountChanged.Broadcast(Count, StageConfig ? StageConfig->TargetCaptureCount : 0);
	EvaluateStageClearCondition();
}

void ABoarGameMode::RequestStageClear()
{
	if (StageState == EBoarStageState::Playing && StageConfig && StageConfig->TargetCaptureCount > 0
		&& CapturedBoarCount >= StageConfig->TargetCaptureCount) bStageClearRequested = true;
}

void ABoarGameMode::RequestGameOver()
{
	if (StageState == EBoarStageState::Playing) bGameOverRequested = true;
}

void ABoarGameMode::HandleWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || TickType == LEVELTICK_PauseTick) return;
	if (StageState == EBoarStageState::Playing) StageRunData.ClearTimeSeconds += DeltaSeconds;
	ResolveStageEnd();
}

void ABoarGameMode::ResolveStageEnd()
{
	const EBoarStageState Next = ResolveBoarStageEnd(StageState, bStageClearRequested, bGameOverRequested);
	if (Next == StageState) return;
	StageState = Next;
	SetStageActorsStopped(true);
	if (StageState == EBoarStageState::Cleared)
	{
		RetryClearSave();
		OnStageCleared();
	}
	else
	{
		// GameOverでは今回の未確定進行を破棄し、永続データには触れません。
		StageRunData = FStageRunData();
		OnGameOver();
	}
	if (!bWaitForStageEndPresentation) CompleteStageEndPresentation();
}

bool ABoarGameMode::RetryClearSave()
{
	if (StageState != EBoarStageState::Cleared) return false;
	if (!bClearSaveSucceeded)
	{
		UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>();
		bClearSaveSucceeded = Instance && Instance->CommitClearedRun(StageRunData);
		if (!bClearSaveSucceeded) UE_LOG(LogTemp, Error, TEXT("[Stage] Clear save pending. Configure BoarGameInstance / StageId or retry saving."));
	}
	if (bClearSaveSucceeded && bEndPresentationCompleted) CompleteStageEndPresentation();
	return bClearSaveSucceeded;
}

void ABoarGameMode::CompleteStageIntroduction()
{
	if (!bStageInitializationComplete || StageState != EBoarStageState::Preparing) return;
	StageState = EBoarStageState::Playing;
	SetStageActorsStopped(false);
	UE_LOG(LogTemp, Log, TEXT("[StageInit] Playing: stage progression enabled"));
	RefreshHousedBoarCount();
}

void ABoarGameMode::CompleteStageEndPresentation()
{
	if (StageState != EBoarStageState::Cleared && StageState != EBoarStageState::GameOver) return;
	bEndPresentationCompleted = true;
	if (bEndUIShown || (StageState == EBoarStageState::Cleared && !bClearSaveSucceeded)) return;
	ABoarPlayerController* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC) return;
	bEndUIShown = true;
	if (StageState == EBoarStageState::Cleared) PC->HandleStageCleared(StageRunData, StageConfig->TargetCaptureCount);
	else PC->HandleStageGameOver();
}

void ABoarGameMode::SetStageActorsStopped(bool bStopped)
{
	if (ABoarPlayerController* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
		PC->SetStageInputBlocked(bStopped);
	for (TActorIterator<ABoarPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (bStopped)
		{
			It->InterruptGadgetUse();
			It->StopDash();
			It->GetCharacterMovement()->StopMovementImmediately();
			It->GetCharacterMovement()->DisableMovement();
		}
		else It->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		It->SetCanBeDamaged(!bStopped);
	}
	for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
	{
		if (AAIController* AI = Cast<AAIController>(It->GetController()))
		{
			if (bStopped) AI->StopMovement();
			if (UBrainComponent* Brain = AI->GetBrainComponent())
			{
				if (bStopped) Brain->StopLogic(TEXT("Stage stopped"));
				else if (!It->IsCaptured()) Brain->RestartLogic();
			}
		}
		It->GetCharacterMovement()->StopMovementImmediately();
		if (bStopped) It->GetCharacterMovement()->DisableMovement();
		else if (!It->IsCaptured()) It->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	for (TActorIterator<ACage> It(GetWorld()); It; ++It) It->SetStageStopped(bStopped);
	for (TActorIterator<AGadgetBase> It(GetWorld()); It; ++It) It->SetStageStopped(bStopped);
}

bool ABoarGameMode::RecordSpecialCoin(FName CoinId)
{
	if (!CanAdvanceStage() || CoinId.IsNone() || StageRunData.SpecialCoinIds.Contains(CoinId)) return false;
	StageRunData.SpecialCoinIds.Add(CoinId);
	return true;
}

void ABoarGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FWorldDelegates::OnWorldTickEnd.Remove(WorldTickEndHandle);
	Super::EndPlay(EndPlayReason);
}

void ABoarGameMode::SpawnConfiguredSpecialCoins()
{
	if (!StageConfig || StageConfig->SpecialCoinDefinitions.IsEmpty()) return;
	if (!SpecialCoinClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Stage] SpecialCoinDefinitions require SpecialCoinClass."));
		return;
	}
	TMap<FName, ABoarSpawnPoint*> Points;
	TSet<FName> DuplicatePoints;
	for (TActorIterator<ABoarSpawnPoint> It(GetWorld()); It; ++It)
	{
		if (It->SpawnPointId.IsNone()) continue;
		if (Points.Contains(It->SpawnPointId)) DuplicatePoints.Add(It->SpawnPointId);
		Points.Add(It->SpawnPointId, *It);
	}
	TMap<FName, int32> IdCounts;
	for (const FSpecialCoinDefinition& Entry : StageConfig->SpecialCoinDefinitions) ++IdCounts.FindOrAdd(Entry.SpecialCoinId);
	for (TActorIterator<ASpecialCoin> It(GetWorld()); It; ++It)
		if (!It->SpecialCoinId.IsNone()) ++IdCounts.FindOrAdd(It->SpecialCoinId);
	const UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>();
	const UBoarSaveGame* Save = Instance ? Instance->GetProgress() : nullptr;
	for (const FSpecialCoinDefinition& Entry : StageConfig->SpecialCoinDefinitions)
	{
		ABoarSpawnPoint* Point = Points.FindRef(Entry.SpawnPointId);
		if (Entry.SpecialCoinId.IsNone() || IdCounts.FindRef(Entry.SpecialCoinId) != 1
			|| !Point || DuplicatePoints.Contains(Entry.SpawnPointId))
		{
			UE_LOG(LogTemp, Error, TEXT("[Stage] Invalid/duplicate coin or spawn point: %s / %s"),
				*Entry.SpecialCoinId.ToString(), *Entry.SpawnPointId.ToString());
			continue;
		}
		if (Save && Save->SpecialCoinIds.Contains(Entry.SpecialCoinId)) continue;
		ASpecialCoin* Coin = GetWorld()->SpawnActorDeferred<ASpecialCoin>(SpecialCoinClass, Point->GetActorTransform());
		if (Coin)
		{
			Coin->SpecialCoinId = Entry.SpecialCoinId;
			UGameplayStatics::FinishSpawningActor(Coin, Point->GetActorTransform());
		}
	}
}
