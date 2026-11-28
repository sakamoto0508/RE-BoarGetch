#include "Core/BoarGameMode.h"
#include "REBoarGetch.h"
#include "Audio/BoarAudioManagerSubsystem.h"

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
#include "TimerManager.h"

namespace
{
	/** 開始・終了演出の完了待ちTimeoutの下限（秒）。0以下だとTimerが即座に切れるため。 */
	constexpr float MinStagePresentationTimeoutSeconds = 0.1f;
}

ABoarGameMode::ABoarGameMode()
{
	PlayerControllerClass = ABoarPlayerController::StaticClass();

	DefaultPawnClass = ABoarPlayerCharacter::StaticClass();
}

void ABoarGameMode::HandleBoarCaptured(ABoarBase* Boar, bool bStartsNewCaptureUse)
{
	// 演出なしの捕獲は、この時点が捕獲成功イベント。
	if (ConfirmCapture(Boar))
		RecordCaptureOrder(Boar, bStartsNewCaptureUse);
}

void ABoarGameMode::RecordCaptureOrder(const ABoarBase* Boar, bool bStartsNewCaptureUse)
{
	if (!IsValid(Boar) || Boar->BoarUniqueId.IsNone())
		return;
	// 時刻・フレームではなく使用単位の通し番号を使う。Gadgetは使用ごとの最初の捕獲成功だけをtrueで通知するため
	// （NetGadgetのbSuccessFeedbackPlayedThisUse）、同じ一振りの捕獲は判定窓の別フレームでも同じ番号になる。
	if (bStartsNewCaptureUse || CaptureEventOrder == 0)
		++CaptureEventOrder;
	StageRunData.LastCaptureOrderByBoar.Add(Boar->BoarUniqueId, CaptureEventOrder);
}

void ABoarGameMode::SnapshotHousedBoars()
{
	if (StageRunData.bHousedSnapshotTaken)
		return;
	// 正本は各Cageの収容配列。演出中（確定待ち）・解放済み・逃走中・即解放の個体はここに含まれない。
	TArray<FName> Ids;

	for (TActorIterator<ACage> It(GetWorld()); It; ++It)
		It->AppendHousedBoarUniqueIds(Ids);

	Ids.Sort([](const FName A, const FName B) { return A.LexicalLess(B); });
	StageRunData.HousedBoarUniqueIds = MoveTemp(Ids);
	StageRunData.bHousedSnapshotTaken = true;
}

bool ABoarGameMode::ConfirmCapture(ABoarBase* Boar)
{
	if (!IsValid(Boar) || !Boar->IsCaptured() || !CanAdvanceStage() || HandledCapturedBoars.Contains(Boar))
		return false;
	// 確定待ちだった場合も、確定済みへ移す（同じBoarを両方の正本に置かない）。
	PendingCapturedBoars.Remove(Boar);
	HandledCapturedBoars.Add(Boar);

	BoarCaptureResolution::Resolve(GetWorld(), Boar, StageRunData, HealPickupClass, HealItemDropChance);
	RefreshHousedBoarCount();
	return true;
}

void ABoarGameMode::RegisterPendingCapture(ABoarBase* Boar, bool bStartsNewCaptureUse)
{
	if (!IsValid(Boar) || !Boar->IsCaptured() || !CanAdvanceStage() || HandledCapturedBoars.Contains(Boar))
		return;
	// 演出ありの捕獲は、確定待ちに登録した時点が捕獲成功イベント（確定は演出後）。
	// 二重登録では期限を延ばさない（最初の登録から上限時間で必ず確定させる）。
	if (PendingCapturedBoars.Contains(Boar))
		return;

	PendingCapturedBoars.Add(Boar, GetWorld()->GetTimeSeconds() + PendingCaptureTimeoutSeconds);
	RecordCaptureOrder(Boar, bStartsNewCaptureUse);
}

void ABoarGameMode::ConfirmExpiredPendingCaptures(double Now)
{
	TArray<TWeakObjectPtr<ABoarBase>> Expired;

	for (const TPair<TWeakObjectPtr<ABoarBase>, double>& Pending : PendingCapturedBoars)
		if (Pending.Value <= Now)
			Expired.Add(Pending.Key);

	if (Expired.IsEmpty())
		return;

	UE_LOG(LogREBoarGetch, Warning, TEXT("[Capture] 捕獲演出から%.1f秒以内に完了通知が届かなかったため、%d体の捕獲を確定します。"),
		PendingCaptureTimeoutSeconds, Expired.Num());
	CompletePendingCaptures(Expired);
}

UCapturePresentationComponent* ABoarGameMode::FindCapturePresentation(const UWorld* World)
{
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->FindComponentByClass<UCapturePresentationComponent>() : nullptr;
}

void ABoarGameMode::BeginCaptureResolution(ABoarBase* Boar, bool bStartsNewCaptureUse)
{
	UWorld* World = Boar ? Boar->GetWorld() : nullptr;
	if (!World)
		return;

	ABoarGameMode* Mode = World->GetAuthGameMode<ABoarGameMode>();
	if (UCapturePresentationComponent* Presentation = FindCapturePresentation(World);
		Presentation && Presentation->PresentCapturedBoar(Boar, bStartsNewCaptureUse))
	{
		if (Mode)
			Mode->RegisterPendingCapture(Boar, bStartsNewCaptureUse);

		return;
	}

	if (Mode)
		Mode->HandleBoarCaptured(Boar, bStartsNewCaptureUse);
}

TArray<ABoarBase*> ABoarGameMode::CompletePendingCaptures(const TArray<TWeakObjectPtr<ABoarBase>>& Boars)
{
	TArray<ABoarBase*> Confirmed;

	for (const TWeakObjectPtr<ABoarBase>& Boar : Boars)
	{
		if (PendingCapturedBoars.Remove(Boar) == 0)
			continue;
		// この一振りの途中で目標に達したら、その時点の収容数を守るため以降の捕獲確定を止める。
		if (ConfirmCapture(Boar.Get()))
			Confirmed.Add(Boar.Get());
	}

	// 確定できた場合はConfirmCapture内で再集計・通知済み。1体も確定しなかった場合だけ従来どおり再集計する。
	if (!Boars.IsEmpty() && Confirmed.IsEmpty())
		RefreshHousedBoarCount();

	return Confirmed;
}

bool ABoarGameMode::IsCapturePending(const ABoarBase* Boar) const
{
	return PendingCapturedBoars.Contains(TWeakObjectPtr<ABoarBase>(const_cast<ABoarBase*>(Boar)));
}

void ABoarGameMode::DiscardPendingCapture(const TWeakObjectPtr<ABoarBase>& Boar)
{
	PendingCapturedBoars.Remove(Boar);
}

void ABoarGameMode::StartPlay()
{
	if (bStartupAttempted)
		return;

	bStartupAttempted = true;
	StageRunData = FStageRunData();
	StageRunData.StageId = StageConfig ? StageConfig->StageId : NAME_None;
	CaptureEventOrder = 0;

	// 前のLevel（Lobby等）から続くBGMは、このStageの曲でなければ読み込み直後に止める。
	// Stage BGM自体はPlaying開始時（CompleteStageIntroduction）にフェードインする。
	if (StageConfig)
		if (auto* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>() : nullptr)
			Audio->StopBGMUnless(StageConfig->StageBGM, StageConfig->PreviousBGMFadeOutSeconds);

	// UEがActorへBeginPlayを配る前に必須依存を準備する。
	// 各ActorのBeginPlay順ではなく、この処理の成否をGameplay開始の根拠にする。
	const bool bPrepared = PrepareStage();
	Super::StartPlay();

	// BeginPlayで追加された装備・演出Actorも含め、開始可否が確定するまで停止を維持する。
	SetStageActorsStopped(true);

	if (!bPrepared)
	{
		if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
			PC->HandleStageInitializationFailed(InitializationError);

		OnStageInitializationFailed(InitializationError);
		return;
	}

	bStageInitializationComplete = true;
	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] 準備中: 初期化完了"));
	WorldTickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ABoarGameMode::HandleWorldTickEnd);

	if (bWaitForStageIntroduction && StageConfig)
	{
		// 完了通知が同期で返る場合にも解除できるよう、BPイベントより先に監視を開始する。
		GetWorldTimerManager().SetTimer(IntroductionTimeoutHandle, this,
			&ABoarGameMode::HandleIntroductionTimeout,
			FMath::Max(MinStagePresentationTimeoutSeconds, StageIntroductionTimeoutSeconds), false);
		OnStageIntroductionRequested();
	}
	else
		CompleteStageIntroduction();
}

bool ABoarGameMode::PrepareStage()
{
	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] 1/4 設定と進行データを検証"));

	if (!ValidateStageSetup())
		return false;

	SetStageActorsStopped(true);
	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] 2/4 設定されたActorをSpawn"));

	// 生成方法はBoarStageSpawnerへ委譲し、GameModeは順序と成否だけを判断します。
	const UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>();

	if (!BoarStageSpawner::SpawnConfiguredBoars(GetWorld(), StageConfig)
		|| !BoarStageSpawner::SpawnConfiguredSpecialCoins(GetWorld(), StageConfig, SpecialCoinClass,
			Instance ? Instance->GetProgress() : nullptr))
		return FailInitialization(NSLOCTEXT("StageInit", "SpawnFailed", "ステージの配置を準備できませんでした。"));

	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] 3/4 檻・Boar・PlayerのHP・装備構成を初期化"));

	if (!InitializeStageActors())
		return false;

	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] 4/4 BeginPlayと導入演出の準備完了"));
	return true;
}

bool ABoarGameMode::ValidateStageSetup()
{
	// StageConfigの無い検証用マップでも、配置済みActorと既定の装備で遊べるようにする。
	if (!StageConfig)
		return true;
	if (StageConfig->StageId.IsNone() || StageConfig->TargetCaptureCount < 0)
		return FailInitialization(NSLOCTEXT("StageInit", "InvalidConfig", "ステージ設定が正しくありません。"));

	const auto* Instance = GetGameInstance<UBoarGameInstance>();
	if (!Instance || !Instance->IsProgressReady())
		return FailInitialization(NSLOCTEXT("StageInit", "ProgressUnavailable", "進行データを読み込めませんでした。"));

	return true;
}

bool ABoarGameMode::InitializeStageActors()
{
	// 個々の設定方法は担当APIへ委譲し、この関数は必須Actorの準備結果を集約する。
	int32 CageCount = 0, BoarCount = 0, PlayerCount = 0;

	for (TActorIterator<ACage> It(GetWorld()); It; ++It)
	{
		It->InitializeForStage();
		++CageCount;
	}

	for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
	{
		It->InitializeForStage();

		if (!It->IsCaptured())
			++BoarCount;
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
	UE_LOG(LogREBoarGetch, Error, TEXT("[StageInit] %s"), *Reason.ToString());
	return false;
}

void ABoarGameMode::EvaluateStageClearCondition()
{
	if (bStageClearRequested || StageConfig == nullptr)
		return;

	const int32 TargetCaptureCount = StageConfig->TargetCaptureCount;

	// 目標未設定の検証マップが開始直後にクリアしないよう、0以下は判定を無効にする。
	if (TargetCaptureCount <= 0 || CapturedBoarCount < TargetCaptureCount)
		return;

	RequestStageClear();
}

void ABoarGameMode::HandlePlayerDeath(ABoarPlayerCharacter* PlayerCharacter)
{
	(void)PlayerCharacter;
	RequestGameOver();
}

bool ABoarGameMode::IsStageProgressAllowed(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const ABoarGameMode* Mode = World ? World->GetAuthGameMode<ABoarGameMode>() : nullptr;
	return !Mode || Mode->CanAdvanceStage();
}

void ABoarGameMode::RefreshHousedBoarCount()
{
	if (!CanAdvanceStage())
		return;

	int32 Count = 0;

	for (TActorIterator<ACage> It(GetWorld()); It; ++It)
		Count += It->GetCapturedBoarCount();

	for (auto It = HandledCapturedBoars.CreateIterator(); It; ++It)
		if (!It->IsValid() || !It->Get()->IsCaptured())
			It.RemoveCurrent();

	CapturedBoarCount = Count;
	StageRunData.CurrentHousedCount = Count;
	OnCapturedBoarCountChanged.Broadcast(Count, StageConfig ? StageConfig->TargetCaptureCount : 0);
	EvaluateStageClearCondition();
}

void ABoarGameMode::RequestStageClear()
{
	// 勝敗は収容数で確定する。残っている受信演出は終了遷移時に打ち切り、死亡要求よりClearを優先する。
	if (StageState == EBoarStageState::Playing && StageConfig && StageConfig->TargetCaptureCount > 0
		&& CapturedBoarCount >= StageConfig->TargetCaptureCount)
		bStageClearRequested = true;
}

void ABoarGameMode::RequestGameOver()
{
	if (StageState == EBoarStageState::Playing)
		bGameOverRequested = true;
}

void ABoarGameMode::HandleWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || TickType == LEVELTICK_PauseTick)
		return;
	if (StageState == EBoarStageState::Playing)
		StageRunData.ClearTimeSeconds += DeltaSeconds;

	if (!PendingCapturedBoars.IsEmpty())
		ConfirmExpiredPendingCaptures(World->GetTimeSeconds());

	ResolveStageEnd();
}

void ABoarGameMode::ResolveStageEnd()
{
	// 要求の到着順で勝敗を決めず、フレーム終端でClear優先を一度だけ適用する。
	const EBoarStageState Next = ResolveBoarStageEnd(StageState, bStageClearRequested, bGameOverRequested);
	if (Next == StageState)
		return;

	// 捕獲演出の中断から確定処理に戻っても、勝敗決定後の収容数を変えないよう先に状態を確定する。
	StageState = Next;

	if (bWaitForStageEndPresentation)
	{
		// BPから同期で完了通知が来てもタイマーが残らないよう、演出イベントより先に予約する。
		GetWorldTimerManager().SetTimer(EndPresentationTimeoutHandle, this,
			&ABoarGameMode::HandleEndPresentationTimeout,
			FMath::Max(MinStagePresentationTimeoutSeconds, StageEndPresentationTimeoutSeconds), false);
	}

	if (UCapturePresentationComponent* Presentation = FindCapturePresentation(GetWorld()))
		Presentation->CancelPresentation();
	PendingCapturedBoars.Reset();
	SetStageActorsStopped(true);

	if (StageState == EBoarStageState::Cleared)
	{
		// 収容数とClear判定に使ったCageの状態から、登録対象を保存より先に確定する。
		SnapshotHousedBoars();
		RetryClearSave();
		OnStageCleared();

		// 勝利演出はClear専用です。完了時にComponentがCompleteStageEndPresentationを呼びます。
		// BP側で独自演出を待つ設定（bWaitForStageEndPresentation）の場合はそちらを優先します。
		if (bUseStageClearPresentation && !bWaitForStageEndPresentation)
			if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
				if (PC->StageClearPresentation && PC->StageClearPresentation->StartPresentation())
					return;
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
				if (PC->GameOverPresentation && PC->GameOverPresentation->StartPresentation())
					return;
	}

	if (!bWaitForStageEndPresentation)
		CompleteStageEndPresentation();
}

bool ABoarGameMode::RetryClearSave()
{
	// 保存と演出は別の完了条件。再試行で成功した場合もResultは一度だけ表示する。
	if (StageState != EBoarStageState::Cleared)
		return false;
	if (!bClearSaveSucceeded)
	{
		UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>();
		bClearSaveSucceeded = Instance && Instance->CommitClearedRun(StageRunData);
		if (!bClearSaveSucceeded) UE_LOG(LogREBoarGetch, Error,
			TEXT(
				"[Stage] Clear save pending. Configure BoarGameInstance / StageId or retry saving."
			));
	}

	if (bClearSaveSucceeded && bEndPresentationCompleted)
		CompleteStageEndPresentation();

	// Result表示後の再試行で保存できた場合は、Resultの警告を消して通常の操作へ戻す。
	if (bClearSaveSucceeded && bEndUIShown)
		if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
			PC->HandleClearSaveRecovered();

	return bClearSaveSucceeded;
}

void ABoarGameMode::CompleteStageIntroduction()
{
	// BPから早期・重複通知が来ても、準備失敗や終了済み状態をPlayingへ戻さない。
	if (!bStageInitializationComplete || StageState != EBoarStageState::Preparing)
		return;

	GetWorldTimerManager().ClearTimer(IntroductionTimeoutHandle);
	StageState = EBoarStageState::Playing;
	SetStageActorsStopped(false);

	// Stage BGMはPlaying開始からフェードイン。Retryでも開始状態から鳴らすため頭から再生し直す。
	// Clear / GameOverでの停止・ジングルは各Presentation BP（StopStageBGM等）が担当する。
	if (StageConfig && StageConfig->StageBGM)
		if (auto* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>() : nullptr)
			Audio->ApplyLevelBGM(this, StageConfig->StageBGM, StageConfig->StageBGMFadeInSeconds, 0.0f, true);
	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] プレイ中: ステージ進行を有効化"));
	RefreshHousedBoarCount();
}

void ABoarGameMode::CompleteStageEndPresentation()
{
	if (StageState != EBoarStageState::Cleared && StageState != EBoarStageState::GameOver)
		return;

	GetWorldTimerManager().ClearTimer(EndPresentationTimeoutHandle);
	bEndPresentationCompleted = true;

	// S-1：保存に失敗してもResultは出す。警告と再試行・保存せず戻るの操作はResult側が持つ。
	if (bEndUIShown)
		return;

	ABoarPlayerController* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC)
		return;

	bEndUIShown = true;

	if (StageState == EBoarStageState::Cleared)
	{
		// 保存前の段階で失敗するとNEW判定が固定されていない。表示のために固定済み扱いにする（NEWは空のまま、保存もしない）。
		if (!StageRunData.bResultFrozen)
		{
			UE_LOG(LogREBoarGetch, Error, TEXT("[Stage] Clear結果を確定できなかったため、NEW表示なしでResultを出します（保存は失敗扱い）。"));
			StageRunData.bResultFrozen = true;
		}

		PC->HandleStageCleared(StageRunData, StageConfig->TargetCaptureCount, bClearSaveSucceeded);
	}
	else
		PC->HandleStageGameOver();
}

void ABoarGameMode::HandleIntroductionTimeout()
{
	if (!bStageInitializationComplete || StageState != EBoarStageState::Preparing)
		return;

	UE_LOG(LogREBoarGetch, Warning, TEXT("[Stage] 導入演出がタイムアウトしたため、ゲームを続行します。"));
	OnStagePresentationTimedOut(true);
	CompleteStageIntroduction();
}

void ABoarGameMode::HandleEndPresentationTimeout()
{
	if (bEndPresentationCompleted || (StageState != EBoarStageState::Cleared && StageState !=
		EBoarStageState::GameOver))
		return;

	UE_LOG(LogREBoarGetch, Warning, TEXT("[Stage] 終了演出がタイムアウトしたため、終了UIへ進みます。"));
	OnStagePresentationTimedOut(false);
	CompleteStageEndPresentation();
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
		else
			It->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

		It->SetCanBeDamaged(!bStopped);
	}

	for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
	{
		if (AAIController* AI = Cast<AAIController>(It->GetController()))
		{
			if (bStopped)
				AI->StopMovement();

			if (UBrainComponent* Brain = AI->GetBrainComponent())
			{
				if (bStopped)
					Brain->StopLogic(TEXT("Stage stopped"));
				else if
				(!It->IsCaptured())
					Brain->RestartLogic();
			}
		}

		It->GetCharacterMovement()->StopMovementImmediately();

		if (bStopped)
		{
			It->GetCharacterMovement()->DisableMovement();
			It->StopBoarLoopingSounds();
		}
		else if (!It->IsCaptured())
			It->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	for (TActorIterator<ACage> It(GetWorld()); It; ++It)
		It->SetStageStopped(bStopped);

	// Cooldownの正本は各PawnのGadgetComponent。装備していないガジェットのCooldownも一緒に止める。
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		if (UGadgetComponent* Gadgets = It->FindComponentByClass<UGadgetComponent>())
			Gadgets->SetCooldownsStopped(bStopped);

	// Componentを持たない所有者のガジェットは、従来どおりActor自身のTimerを止める。
	for (TActorIterator<AGadgetBase> It(GetWorld()); It; ++It)
		It->SetStageStopped(bStopped);
}

bool ABoarGameMode::RecordSpecialCoin(FName CoinId)
{
	if (!CanAdvanceStage() || CoinId.IsNone() || StageRunData.SpecialCoinIds.Contains(CoinId))
		return false;

	StageRunData.SpecialCoinIds.Add(CoinId);
	return true;
}

void ABoarGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(IntroductionTimeoutHandle);
	GetWorldTimerManager().ClearTimer(EndPresentationTimeoutHandle);
	FWorldDelegates::OnWorldTickEnd.Remove(WorldTickEndHandle);
	Super::EndPlay(EndPlayReason);
}
