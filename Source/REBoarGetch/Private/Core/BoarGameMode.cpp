// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BoarGameMode.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"
#include "Core/BoarCaptureResolution.h"
#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Core/BoarStageSpawner.h"
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
#include "Component/CapturePresentationComponent.h"
#include "Component/StageClearPresentationComponent.h"
#include "Component/GameOverPresentationComponent.h"
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
	// 捕獲成立時の演出や報酬付与をBoarCaptureResolutionに委譲する。
	BoarCaptureResolution::Resolve(GetWorld(), Boar, StageRunData, HealPickupClass, HealItemDropChance);
	RefreshHousedBoarCount();
}

void ABoarGameMode::StartPlay()
{
	if (bStartupAttempted) return;
	bStartupAttempted = true;
	StageRunData = FStageRunData();
	StageRunData.StageId = StageConfig ? StageConfig->StageId : NAME_None;

	// All required actors and player dependencies are prepared before UE
	// dispatches BeginPlay. Actor/component BeginPlay only completes local setup.
	const bool bPrepared = PrepareStage();
	Super::StartPlay();
	// BeginPlay can create presentation/gadget actors; keep them stopped too.
	SetStageActorsStopped(true);
	if (!bPrepared)
	{
		if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
			PC->HandleStageInitializationFailed(InitializationError);
		OnStageInitializationFailed(InitializationError);
		return;
	}
	bStageInitializationComplete = true;
	UE_LOG(LogTemp, Log, TEXT("[StageInit] Preparing: initialization complete"));
	WorldTickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ABoarGameMode::HandleWorldTickEnd);
	if (bWaitForStageIntroduction && StageConfig)
	{
		OnStageIntroductionRequested();
	}
	else CompleteStageIntroduction();
}

bool ABoarGameMode::PrepareStage()
{
	UE_LOG(LogTemp, Log, TEXT("[StageInit] 1/4 Validate configuration and progress"));
	if (!ValidateStageSetup()) return false;
	SetStageActorsStopped(true);
	UE_LOG(LogTemp, Log, TEXT("[StageInit] 2/4 Spawn configured actors"));
	// 生成方法はBoarStageSpawnerへ委譲し、GameModeは順序と成否だけを判断します。
	const UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>();
	if (!BoarStageSpawner::SpawnConfiguredBoars(GetWorld(), StageConfig)
		|| !BoarStageSpawner::SpawnConfiguredSpecialCoins(GetWorld(), StageConfig, SpecialCoinClass, Instance ? Instance->GetProgress() : nullptr))
		return FailInitialization(NSLOCTEXT("StageInit", "SpawnFailed", "ステージの配置を準備できませんでした。"));
	UE_LOG(LogTemp, Log, TEXT("[StageInit] 3/4 Initialize cages, boars, player health and loadout"));
	if (!InitializeStageActors()) return false;
	UE_LOG(LogTemp, Log, TEXT("[StageInit] 4/4 Ready for BeginPlay and introduction"));
	return true;
}

bool ABoarGameMode::ValidateStageSetup()
{
	// Unconfigured sandbox maps keep supporting placed actors and default equipment.
	if (!StageConfig) return true;
	if (StageConfig->StageId.IsNone() || StageConfig->TargetCaptureCount < 0)
		return FailInitialization(NSLOCTEXT("StageInit", "InvalidConfig", "ステージ設定が正しくありません。"));
	const auto* Instance = GetGameInstance<UBoarGameInstance>();
	if (!Instance || !Instance->IsProgressReady())
		return FailInitialization(NSLOCTEXT("StageInit", "ProgressUnavailable", "進行データを読み込めませんでした。"));
	return true;
}

bool ABoarGameMode::InitializeStageActors()
{
	int32 CageCount = 0, BoarCount = 0, PlayerCount = 0;
	for (TActorIterator<ACage> It(GetWorld()); It; ++It)
	{
		It->InitializeForStage();
		++CageCount;
	}
	for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
	{
		It->InitializeForStage();
		if (!It->IsCaptured()) ++BoarCount;
	}
	const auto Context = BoarGadgetLoadoutInitializer::ResolveContext(GetWorld());
	for (TActorIterator<ABoarPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->InitializeForPlay(Context))
			return FailInitialization(NSLOCTEXT("StageInit", "PlayerFailed", "プレイヤーの体力・装備を準備できませんでした。"));
		++PlayerCount;
	}
	if (StageConfig && PlayerCount == 0)
		return FailInitialization(NSLOCTEXT("StageInit", "NoPlayer", "プレイヤーを生成できませんでした。"));
	if (StageConfig && StageConfig->TargetCaptureCount > 0 &&
		(CageCount == 0 || BoarCount < StageConfig->TargetCaptureCount))
		return FailInitialization(NSLOCTEXT("StageInit", "UnreachableTarget", "捕獲目標に必要な檻・ボアが不足しています。"));
	return true;
}

bool ABoarGameMode::FailInitialization(const FText& Reason)
{
	InitializationError = Reason;
	UE_LOG(LogTemp, Error, TEXT("[StageInit] %s"), *Reason.ToString());
	return false;
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
	for (TActorIterator<ACage> It(GetWorld()); It; ++It) 
		Count += It->GetCapturedBoarCount();
	for (auto It = HandledCapturedBoars.CreateIterator(); It; ++It)
		if (!It->IsValid() || !It->Get()->IsCaptured())		It.RemoveCurrent();
	CapturedBoarCount = Count;
	StageRunData.CurrentHousedCount = Count;
	OnCapturedBoarCountChanged.Broadcast(Count, StageConfig ? StageConfig->TargetCaptureCount : 0);
	EvaluateStageClearCondition();
}

void ABoarGameMode::RequestStageClear()
{
	// StageClearの演出が残っている場合は、演出完了までクリアを保留します。
	if (auto* PC = GetWorld()->GetFirstPlayerController())
		if (auto* Presentation = PC->FindComponentByClass<UCapturePresentationComponent>())
			if (Presentation->HasPendingCaptures()) return;
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
	if (auto* PC = GetWorld()->GetFirstPlayerController())
		if (auto* Presentation = PC->FindComponentByClass<UCapturePresentationComponent>()) Presentation->CancelPresentation();
	SetStageActorsStopped(true);
	if (StageState == EBoarStageState::Cleared)
	{
		RetryClearSave();
		OnStageCleared();
		// 勝利演出はClear専用です。完了時にComponentがCompleteStageEndPresentationを呼びます。
		// BP側で独自演出を待つ設定（bWaitForStageEndPresentation）の場合はそちらを優先します。
		if (bUseStageClearPresentation && !bWaitForStageEndPresentation)
			if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
				if (PC->StageClearPresentation && PC->StageClearPresentation->StartPresentation()) return;
	}
	else
	{
		// GameOverでは今回の未確定進行を破棄し、永続データには触れません。
		StageRunData = FStageRunData();
		OnGameOver();
		// GameOver演出もClearと同じ完了通知（CompleteStageEndPresentation）でUIへ進みます。
		// BP側で独自演出を待つ設定（bWaitForStageEndPresentation）の場合はそちらを優先します。
		if (bUseGameOverPresentation && !bWaitForStageEndPresentation)
			if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
				if (PC->GameOverPresentation && PC->GameOverPresentation->StartPresentation()) return;
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
	if (StageState == EBoarStageState::Cleared) 
		PC->HandleStageCleared(StageRunData, StageConfig->TargetCaptureCount);
	else 
		PC->HandleStageGameOver();
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
	for (TActorIterator<ACage> It(GetWorld()); It; ++It) 
		It->SetStageStopped(bStopped);
	for (TActorIterator<AGadgetBase> It(GetWorld()); It; ++It) 
		It->SetStageStopped(bStopped);
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

