#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/BoarGameMode.h"
#include "Core/BoarGameInstance.h"
#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Component/CapturePresentationComponent.h"
#include "Player/BoarPlayerController.h"
#include "TimerManager.h"
#include "Gadget/GadgetBase.h"
#include "Player/BoarPlayerCharacter.h"
#include "Boar/BoarBase.h"
#include "Component/CaptureComponent.h"
#include "Cage/Cage.h"
#include "Stage/StageConfig.h"
#include "Stage/BoarSpawnPoint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FStartupWorld
	{
		UBoarGameInstance* Instance;
		UWorld* World;

		FStartupWorld()
		{
			Instance = NewObject<UBoarGameInstance>(GEngine);
			// Only read an isolated, non-existent slot. Startup must not write saves.
			FindFProperty<FStrProperty>(UBoarGameInstance::StaticClass(), TEXT("SaveSlotName"))
				->SetPropertyValue_InContainer(Instance, TEXT("REBoarGetch_Startup_") + FGuid::NewGuid().ToString());
			Instance->InitializeStandalone();
			World = Instance->GetWorld();
		}

		~FStartupWorld()
		{
			if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			Instance->Shutdown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ABoarGameMode* CreateStage()
		{
			const FURL URL(nullptr, TEXT("?game=/Script/REBoarGetch.BoarGameMode"), TRAVEL_Absolute);
			World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
			return World->GetAuthGameMode<ABoarGameMode>();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarStageInitializationTest, "REBoarGetch.Spec.StageInitialization",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarStageInitializationTest::RunTest(const FString& Parameters)
{
	{
		FStartupWorld Fixture;
		auto* Mode = Fixture.CreateStage();
		auto* Player = Fixture.World->SpawnActor<ABoarPlayerCharacter>();
		if (!TestNotNull(TEXT("Player before BeginPlay"), Player)) return false;
		TestTrue(TEXT("Preparation succeeds before UE BeginPlay"), Mode->PrepareStage());
		TestFalse(TEXT("Preparation does not dispatch BeginPlay"), Player->HasActorBegunPlay());
		TestTrue(TEXT("Health and loadout already ready before BeginPlay"), Player->IsReadyForPlay());
		TestTrue(TEXT("Preparation can safely be repeated"), Mode->PrepareStage());
	}
	{
		FStartupWorld Fixture;
		auto* Mode = Fixture.CreateStage();
		if (!TestNotNull(TEXT("Stage game mode"), Mode)) return false;
		auto* Config = NewObject<UStageConfig>(Mode);
		Config->StageId = TEXT("StartupTest");
		Config->TargetCaptureCount = 1;
		FBoarSpawnDefinition Definition;
		Definition.BoarUniqueId = TEXT("StartupBoar");
		Definition.SpawnPointId = TEXT("StartupPoint");
		Definition.BoarClass = ABoarBase::StaticClass();
		Config->BoarSpawnDefinitions.Add(Definition);
		Mode->StageConfig = Config;
		Mode->bWaitForStageIntroduction = true;
		auto* Point = Fixture.World->SpawnActor<ABoarSpawnPoint>();
		Point->SpawnPointId = Definition.SpawnPointId;
		Point->SetActorLocation(FVector(500, 0, 100));
		Fixture.World->SpawnActor<ACage>(FVector(1000, 0, 0), FRotator::ZeroRotator);
		auto* Player = Fixture.World->SpawnActor<ABoarPlayerCharacter>();
		if (!TestNotNull(TEXT("Placed player"), Player)) return false;
		TestFalse(TEXT("Player is not initialized by construction"), Player->IsReadyForPlay());
		TestFalse(TEXT("Health awaits startup"), Player->GetHealthComponent()->IsHealthInitialized());
		Fixture.World->BeginPlay();
		TestTrue(TEXT("Coordinator completes startup"), Mode->bStageInitializationComplete);
		TestTrue(TEXT("Player dependencies prepared"), Player->IsReadyForPlay());
		TestTrue(TEXT("Equipment prepared before introduction"), Player->GetGadgetComponent()->IsLoadoutInitialized());
		TestTrue(TEXT("Introduction still blocks gameplay"), !Mode->CanAdvanceStage());
		TestTrue(
			TEXT("Preparing until explicit introduction completion"),
			Mode->GetStageState() == EBoarStageState::Preparing);
		TestTrue(
			TEXT("開始演出の通知欠落を監視する"), Fixture.World->GetTimerManager().IsTimerActive(Mode->IntroductionTimeoutHandle));
		int32 Count = 0;
		for (TActorIterator<ABoarBase> It(Fixture.World); It; ++It)
		{
			++Count;
			TestEqual(TEXT("Persistent identity supplied before play"), It->BoarUniqueId, Definition.BoarUniqueId);
			TestTrue(TEXT("Normal BeginPlay dispatched after preparation"), It->HasActorBegunPlay());
		}
		TestEqual(TEXT("Configured boar created once"), Count, 1);
		Mode->CompleteStageIntroduction();
		TestFalse(TEXT("通常完了で開始演出の監視を解除"), Fixture.World->GetTimerManager().IsTimerActive(
			          Mode->IntroductionTimeoutHandle));
		TestTrue(TEXT("Ready stage may start"), Mode->CanAdvanceStage());
		auto* Health = Player->GetHealthComponent();
		Health->TakeDamage(1.f);
		const float DamagedHealth = Health->GetCurrentHealth();
		TestTrue(TEXT("Repeated player initialization is safe"), Player->InitializeForPlay(
			         BoarGadgetLoadoutInitializer::ResolveContext(Fixture.World)));
		TestEqual(TEXT("Repeated initialization does not heal"), Health->GetCurrentHealth(), DamagedHealth);
		Mode->StartPlay();
		Count = 0;
		for (TActorIterator<ABoarBase> It(Fixture.World); It; ++It) ++Count;
		TestEqual(TEXT("Repeated StartPlay never respawns configured actors"), Count, 1);
	}
	{
		FStartupWorld Fixture;
		auto* Mode = Fixture.CreateStage();
		Mode->StageConfig = NewObject<UStageConfig>(Mode); // Missing persistent stage ID.
		AddExpectedError(TEXT("ステージ設定が正しくありません"), EAutomationExpectedErrorFlags::Contains, 1);
		Fixture.World->BeginPlay();
		Mode->CompleteStageIntroduction();
		TestFalse(TEXT("Invalid setup cannot complete initialization"), Mode->bStageInitializationComplete);
		TestFalse(TEXT("Introduction callback cannot bypass failure"), Mode->CanAdvanceStage());
		TestFalse(TEXT("Failure reason is exposed"), Mode->GetInitializationError().IsEmpty());
	}
	{
		FStartupWorld Fixture;
		auto* Mode = Fixture.CreateStage();
		auto* Config = NewObject<UStageConfig>(Mode);
		Config->StageId = TEXT("MissingSpawnTest");
		FBoarSpawnDefinition Definition;
		Definition.BoarClass = ABoarBase::StaticClass();
		Definition.BoarUniqueId = TEXT("Boar");
		Definition.SpawnPointId = TEXT("Missing");
		Config->BoarSpawnDefinitions.Add(Definition);
		Mode->StageConfig = Config;
		AddExpectedError(TEXT("Invalid/duplicate identity or spawn point"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("ステージの配置を準備できませんでした"), EAutomationExpectedErrorFlags::Contains, 1);
		Fixture.World->BeginPlay();
		TestFalse(TEXT("Missing spawn dependency blocks startup"), Mode->bStageInitializationComplete);
		TestFalse(TEXT("Partial spawn does not enter gameplay"), Mode->CanAdvanceStage());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarStageEndRegressionTest, "REBoarGetch.Spec.StageEndRegression",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarStageEndRegressionTest::RunTest(const FString& Parameters)
{
	{
		FStartupWorld Fixture;
		auto* Mode = Fixture.CreateStage();
		auto* PC = Fixture.World->SpawnActor<ABoarPlayerController>();
		if (!TestNotNull(TEXT("捕獲演出を持つController"), PC)) return false;
		PC->CapturePresentation->Receptions.AddDefaulted();
		Mode->StageConfig = NewObject<UStageConfig>(Mode);
		Mode->StageConfig->TargetCaptureCount = 1;
		Mode->CapturedBoarCount = 1;
		Mode->StageState = EBoarStageState::Playing;
		Mode->bWaitForStageEndPresentation = true;
		// 実セーブとUI生成を避け、勝敗と演出待機の制御だけを検証する。
		Mode->bClearSaveSucceeded = true;
		Mode->bEndUIShown = true;
		Mode->RequestGameOver();
		Mode->RequestStageClear();
		TestTrue(TEXT("受信演出中でもClear要求は成立"), Mode->bStageClearRequested);
		TestFalse(TEXT("Clear要求で被弾・檻解放を即時遮断"), Mode->CanAdvanceStage());
		Mode->ResolveStageEnd();
		TestTrue(TEXT("同フレーム死亡よりClearを優先"), Mode->StageState == EBoarStageState::Cleared);
		TestFalse(TEXT("勝敗確定時に残った捕獲演出を解除"), PC->CapturePresentation->HasPendingCaptures());
		TestTrue(TEXT("独自終了演出の待機に上限がある"), Fixture.World->GetTimerManager().IsTimerActive(
			         Mode->EndPresentationTimeoutHandle));
		Mode->HandleEndPresentationTimeout();
		TestTrue(TEXT("終了通知欠落時にも演出待ちを抜ける"), Mode->bEndPresentationCompleted);
		TestFalse(TEXT("終了後は監視を解除"), Fixture.World->GetTimerManager().IsTimerActive(
			          Mode->EndPresentationTimeoutHandle));
		Mode->HandleEndPresentationTimeout();
		TestTrue(TEXT("重複Timeoutでも勝敗を維持"), Mode->StageState == EBoarStageState::Cleared);
	}
	{
		FStartupWorld Fixture;
		auto* Mode = Fixture.CreateStage();
		Mode->bStageInitializationComplete = true;
		Mode->HandleIntroductionTimeout();
		TestTrue(TEXT("開始通知欠落時にもPlayingへ進む"), Mode->CanAdvanceStage());
		Mode->RequestGameOver();
		Mode->bWaitForStageEndPresentation = true;
		Mode->ResolveStageEnd();
		Mode->HandleEndPresentationTimeout();
		TestTrue(TEXT("GameOverも終了通知欠落で待ち続けない"), Mode->bEndPresentationCompleted);
		Mode->CompleteStageIntroduction();
		TestTrue(TEXT("遅延した開始完了で終了状態を戻さない"), Mode->StageState == EBoarStageState::GameOver);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarLoadoutInitializationFailureTest, "REBoarGetch.Spec.LoadoutInitializationFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarLoadoutInitializationFailureTest::RunTest(const FString& Parameters)
{
	FStartupWorld Fixture;
	Fixture.CreateStage();
	auto* Player = Fixture.World->SpawnActor<ABoarPlayerCharacter>();
	if (!TestNotNull(TEXT("Player"), Player)) return false;
	auto* Gadgets = Player->GetGadgetComponent();
	const TArray<TSubclassOf<AGadgetBase>> InvalidSlots = {AGadgetBase::StaticClass(), nullptr, nullptr, nullptr};
	TestFalse(
		TEXT("Missing mesh/socket fails initial equipment"),
		Gadgets->InitializeResolvedLoadout(InvalidSlots, nullptr, false));
	TestFalse(TEXT("Failed equipment is never marked initialized"), Gadgets->IsLoadoutInitialized());
	const TArray<TSubclassOf<AGadgetBase>> EmptySlots = {nullptr, nullptr, nullptr, nullptr};
	TestTrue(
		TEXT("Corrected empty loadout can be initialized"),
		Gadgets->InitializeResolvedLoadout(EmptySlots, nullptr, true));
	TestTrue(TEXT("Successful retry marks initialized"), Gadgets->IsLoadoutInitialized());
	TestTrue(
		TEXT("Later initialization leaves policy intact"),
		Gadgets->InitializeResolvedLoadout(InvalidSlots, nullptr, false));
	TestTrue(TEXT("Temporary loadout is preserved"), Gadgets->IsTemporaryLoadout());
	TestNull(TEXT("Successful empty loadout is preserved"), Gadgets->GetCurrentGadget());
	const auto PlayerClass = LoadClass<ABoarPlayerCharacter>(
		nullptr, TEXT("/Game/BP/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"));
	const auto NetClass = LoadClass<AGadgetBase>(nullptr, TEXT("/Game/BP/Gadget/BP_NetGadget.BP_NetGadget_C"));
	if (!TestNotNull(TEXT("Existing player Blueprint"), PlayerClass) ||
		!TestNotNull(TEXT("Existing net Blueprint"), NetClass))
		return false;
	auto* BlueprintPlayer = Fixture.World->SpawnActor<ABoarPlayerCharacter>(
		PlayerClass, FVector(500, 0, 100), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Existing player before BeginPlay"), BlueprintPlayer)) return false;
	FBoarGadgetLoadoutContext TestContext;
	TestContext.bTemporary = true;
	TestContext.TestGadgetClass = NetClass;
	TestTrue(
		TEXT("Existing mesh/socket supports equipment before BeginPlay"),
		BlueprintPlayer->InitializeForPlay(TestContext));
	TestFalse(TEXT("Equipping does not dispatch player BeginPlay"), BlueprintPlayer->HasActorBegunPlay());
	TestNotNull(TEXT("Initial gadget actor is created"), BlueprintPlayer->GetGadgetComponent()->GetCurrentGadget());
	TestTrue(
		TEXT("Facility test policy reaches the component"),
		BlueprintPlayer->GetGadgetComponent()->IsTemporaryLoadout());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarPendingCaptureTest, "REBoarGetch.Spec.PendingCapture",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarPendingCaptureTest::RunTest(const FString& Parameters)
{
	FStartupWorld Fixture;
	auto* Mode = Fixture.CreateStage();
	if (!TestNotNull(TEXT("Stage game mode"), Mode)) return false;
	Mode->StageState = EBoarStageState::Playing;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Cage = Fixture.World->SpawnActor<ACage>(FVector(500, 0, 0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("収容先のCage"), Cage)) return false;
	Cage->InitializeForStage();
	auto SpawnCaptured = [&](FName Id) -> ABoarBase*
	{
		auto* Boar = Fixture.World->SpawnActor<ABoarBase>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		if (!Boar) return nullptr;
		Boar->BoarUniqueId = Id;
		auto* Capture = Boar->FindComponentByClass<UCaptureComponent>();
		return Capture && Capture->Capture(nullptr) ? Boar : nullptr;
	};

	// 演出完了の通知で、確定待ちの捕獲が一度だけ確定する。
	ABoarBase* First = SpawnCaptured(TEXT("PendingTest_First"));
	if (!TestNotNull(TEXT("捕獲状態のBoar"), First)) return false;
	Mode->RegisterPendingCapture(First);
	TestTrue(TEXT("演出中は確定待ち"), Mode->IsCapturePending(First));
	TestFalse(TEXT("演出中は未確定"), Mode->HandledCapturedBoars.Contains(First));
	TArray<TWeakObjectPtr<ABoarBase>> Batch{First};
	TestEqual(TEXT("演出完了で確定"), Mode->CompletePendingCaptures(Batch).Num(), 1);
	TestFalse(TEXT("確定後は確定待ちに残らない"), Mode->IsCapturePending(First));
	TestTrue(TEXT("確定済みとして記録"), Mode->HandledCapturedBoars.Contains(First));
	TestEqual(TEXT("同じ通知が重なっても二重確定しない"), Mode->CompletePendingCaptures(Batch).Num(), 0);

	// 見た目だけの片付け（Level破棄・対象Destroy）は確定しない。
	ABoarBase* Discarded = SpawnCaptured(TEXT("PendingTest_Discarded"));
	if (!TestNotNull(TEXT("破棄用のBoar"), Discarded)) return false;
	Mode->RegisterPendingCapture(Discarded);
	Mode->DiscardPendingCapture(Discarded);
	TestFalse(TEXT("破棄で確定待ちから外れる"), Mode->IsCapturePending(Discarded));
	TestEqual(TEXT("破棄後の完了通知では確定しない"),
	          Mode->CompletePendingCaptures({TWeakObjectPtr<ABoarBase>(Discarded)}).Num(), 0);
	TestFalse(TEXT("破棄した捕獲は未確定のまま"), Mode->HandledCapturedBoars.Contains(Discarded));

	// Clear要求後（Stage終了処理中）は確定せず、確定待ちにも残さない。
	ABoarBase* Late = SpawnCaptured(TEXT("PendingTest_Late"));
	if (!TestNotNull(TEXT("終了直前のBoar"), Late)) return false;
	Mode->RegisterPendingCapture(Late);
	Mode->bStageClearRequested = true;
	TestEqual(TEXT("Clear要求後は確定しない"), Mode->CompletePendingCaptures({TWeakObjectPtr<ABoarBase>(Late)}).Num(), 0);
	TestFalse(TEXT("確定できなかった捕獲も確定待ちから外す"), Mode->IsCapturePending(Late));
	TestFalse(TEXT("Clear要求後の捕獲は未確定"), Mode->HandledCapturedBoars.Contains(Late));

	// Stage終了確定時は、残った確定待ちを確定せずに破棄する。
	Mode->PendingCapturedBoars.Add(Late);
	Mode->bWaitForStageEndPresentation = true;
	Mode->bClearSaveSucceeded = true;
	Mode->bEndUIShown = true;
	Mode->ResolveStageEnd();
	TestFalse(TEXT("Stage終了で確定待ちを破棄"), Mode->IsCapturePending(Late));
	return true;
}

#endif
