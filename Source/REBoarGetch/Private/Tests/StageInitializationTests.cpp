#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/BoarGameMode.h"
#include "Core/BoarGameInstance.h"
#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Gadget/GadgetBase.h"
#include "Player/BoarPlayerCharacter.h"
#include "Boar/BoarBase.h"
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
		TestTrue(TEXT("Preparing until explicit introduction completion"), Mode->GetStageState() == EBoarStageState::Preparing);
		int32 Count = 0;
		for (TActorIterator<ABoarBase> It(Fixture.World); It; ++It)
		{
			++Count;
			TestEqual(TEXT("Persistent identity supplied before play"), It->BoarUniqueId, Definition.BoarUniqueId);
			TestTrue(TEXT("Normal BeginPlay dispatched after preparation"), It->HasActorBegunPlay());
		}
		TestEqual(TEXT("Configured boar created once"), Count, 1);
		Mode->CompleteStageIntroduction();
		TestTrue(TEXT("Ready stage may start"), Mode->CanAdvanceStage());
		auto* Health = Player->GetHealthComponent();
		Health->TakeDamage(1.f);
		const float DamagedHealth = Health->GetCurrentHealth();
		TestTrue(TEXT("Repeated player initialization is safe"), Player->InitializeForPlay(BoarGadgetLoadoutInitializer::ResolveContext(Fixture.World)));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarLoadoutInitializationFailureTest, "REBoarGetch.Spec.LoadoutInitializationFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoarLoadoutInitializationFailureTest::RunTest(const FString& Parameters)
{
	FStartupWorld Fixture;
	Fixture.CreateStage();
	auto* Player = Fixture.World->SpawnActor<ABoarPlayerCharacter>();
	if (!TestNotNull(TEXT("Player"), Player)) return false;
	auto* Gadgets = Player->GetGadgetComponent();
	const TArray<TSubclassOf<AGadgetBase>> InvalidSlots = { AGadgetBase::StaticClass(), nullptr, nullptr, nullptr };
	TestFalse(TEXT("Missing mesh/socket fails initial equipment"), Gadgets->InitializeResolvedLoadout(InvalidSlots, nullptr, false));
	TestFalse(TEXT("Failed equipment is never marked initialized"), Gadgets->IsLoadoutInitialized());
	const TArray<TSubclassOf<AGadgetBase>> EmptySlots = { nullptr, nullptr, nullptr, nullptr };
	TestTrue(TEXT("Corrected empty loadout can be initialized"), Gadgets->InitializeResolvedLoadout(EmptySlots, nullptr, true));
	TestTrue(TEXT("Successful retry marks initialized"), Gadgets->IsLoadoutInitialized());
	TestTrue(TEXT("Later initialization leaves policy intact"), Gadgets->InitializeResolvedLoadout(InvalidSlots, nullptr, false));
	TestTrue(TEXT("Temporary loadout is preserved"), Gadgets->IsTemporaryLoadout());
	TestNull(TEXT("Successful empty loadout is preserved"), Gadgets->GetCurrentGadget());
	const auto PlayerClass = LoadClass<ABoarPlayerCharacter>(nullptr, TEXT("/Game/BP/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"));
	const auto NetClass = LoadClass<AGadgetBase>(nullptr, TEXT("/Game/BP/Gadget/BP_NetGadget.BP_NetGadget_C"));
	if (!TestNotNull(TEXT("Existing player Blueprint"), PlayerClass) ||
		!TestNotNull(TEXT("Existing net Blueprint"), NetClass)) return false;
	auto* BlueprintPlayer = Fixture.World->SpawnActor<ABoarPlayerCharacter>(PlayerClass, FVector(500, 0, 100), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Existing player before BeginPlay"), BlueprintPlayer)) return false;
	FBoarGadgetLoadoutContext TestContext;
	TestContext.bTemporary = true;
	TestContext.TestGadgetClass = NetClass;
	TestTrue(TEXT("Existing mesh/socket supports equipment before BeginPlay"), BlueprintPlayer->InitializeForPlay(TestContext));
	TestFalse(TEXT("Equipping does not dispatch player BeginPlay"), BlueprintPlayer->HasActorBegunPlay());
	TestNotNull(TEXT("Initial gadget actor is created"), BlueprintPlayer->GetGadgetComponent()->GetCurrentGadget());
	TestTrue(TEXT("Facility test policy reaches the component"), BlueprintPlayer->GetGadgetComponent()->IsTemporaryLoadout());
	return true;
}
#endif
