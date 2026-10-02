#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/BoarGameInstance.h"
#include "Core/BoarFacilityGameMode.h"
#include "Gadget/BoarGadgetSubsystem.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Save/BoarSaveGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/BoarLoadoutPresenter.h"
#include "Player/BoarPlayerController.h"
#include "Player/BoarPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Component/GadgetComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarGadgetProgressTest, "REBoarGetch.Spec.GadgetProgressAndLoadout",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoarGadgetProgressTest::RunTest(const FString &Parameters)
{
	// An isolated, non-existent slot and write prohibition protect the user's actual save.
	auto *Instance = NewObject<UBoarGameInstance>(GEngine);
	Instance->SaveSlotName = TEXT("REBoarGetch_Automation_") + FGuid::NewGuid().ToString();
	const auto NetClass = LoadClass<AGadgetBase>(nullptr, TEXT("/Game/BP/Gadget/BP_NetGadget.BP_NetGadget_C"));
	if (!TestNotNull(TEXT("Existing catalog gadget"), NetClass))
		return false;
	const auto *Definition = NetClass->GetDefaultObject<AGadgetBase>()->GetGadgetDefinition();
	if (!TestNotNull(TEXT("Existing definition"), Definition))
		return false;
	Instance->GadgetCatalog = {NetClass};
	Instance->InitializeStandalone();
	Instance->bSaveLoadFailed = true;
	UWorld *World = Instance->GetWorld();
	auto *Gadgets = Instance->GetSubsystem<UBoarGadgetSubsystem>();
	const FName Id = Definition->GadgetId;
	TestNotNull(TEXT("Gadget subsystem created before GI initial unlock update"), Gadgets);
	if (Gadgets)
	{
		TestTrue(TEXT("Initial unlock from existing catalog"), Gadgets->IsGadgetUnlocked(Id));
		TestTrue(TEXT("GI compatibility facade"), Instance->IsGadgetUnlocked(Id));
		TestFalse(TEXT("Already unlocked cannot unlock again"), Gadgets->CanUnlockGadget(Id));
		TestFalse(TEXT("Unknown ID cannot unlock"), Gadgets->CanUnlockGadget(TEXT("Missing")));
		TestFalse(TEXT("None is never unlocked"), Gadgets->IsGadgetUnlocked(NAME_None));
		TestTrue(TEXT("Four slots including empty accepted"), Gadgets->IsLoadoutValid({Id, NAME_None, NAME_None, NAME_None}));
		TestFalse(TEXT("Wrong slot count rejected"), Gadgets->IsLoadoutValid({Id}));
		TestFalse(TEXT("Duplicate gadget rejected"), Gadgets->IsLoadoutValid({Id, Id, NAME_None, NAME_None}));
		TestFalse(TEXT("Unresolved gadget rejected"), Gadgets->IsLoadoutValid({TEXT("Missing"), NAME_None, NAME_None, NAME_None}));
		TArray<TSubclassOf<AGadgetBase>> Defaults = {NetClass, nullptr, nullptr, nullptr};
		TArray<TSubclassOf<AGadgetBase>> Restored = Defaults;
		Instance->Progress->bHasSavedLoadout = true;
		Instance->Progress->GadgetLoadout = {NAME_None, Id, NAME_None, NAME_None};
		Gadgets->RestoreSavedLoadout(Restored);
		TestTrue(TEXT("Saved empty first slot and catalog class restored"), !Restored[0] && Restored[1] == NetClass);
		Instance->Progress->GadgetLoadout = {TEXT("Missing"), Id, NAME_None, NAME_None};
		Restored = Defaults;
		AddExpectedError(TEXT("Saved loadout has unresolved IDs"), EAutomationExpectedErrorFlags::Contains, 1);
		Gadgets->RestoreSavedLoadout(Restored);
		TestTrue(TEXT("Unknown saved ID keeps full default loadout"), Restored == Defaults);
		TestEqual(TEXT("Unknown saved ID is not rewritten"), Instance->Progress->GadgetLoadout[0], FName(TEXT("Missing")));
		Instance->Progress->GadgetLoadout = {NAME_None, NAME_None, NAME_None, NAME_None};
		Gadgets->RestoreSavedLoadout(Restored);
		TestTrue(TEXT("All empty saved loadout is intentional"), !Restored[0] && !Restored[1] && !Restored[2] && !Restored[3]);
		Instance->Progress->GadgetLoadout = {Id};
		Restored = Defaults;
		Gadgets->RestoreSavedLoadout(Restored);
		TestTrue(TEXT("Malformed saved slot count keeps defaults"), Restored == Defaults);
		Instance->Progress->bHasSavedLoadout = false;
		Instance->Progress->GadgetLoadout.Reset();
		Instance->Progress->UnlockedGadgetIds.Remove(Id);
		TestFalse(TEXT("Locked loadout rejected"), Gadgets->IsLoadoutValid({Id, NAME_None, NAME_None, NAME_None}));
		{
			// Scope restores the in-memory DataAsset field; no package is saved.
			TGuardValue<int32> RequiredCoins(const_cast<UGadgetDataAsset *>(Definition)->RequiredSpecialCoinCount, 2);
			Instance->Progress->SpecialCoinIds = {TEXT("C1")};
			TestFalse(TEXT("Below coin threshold"), Gadgets->CanUnlockGadget(Id));
			Instance->Progress->SpecialCoinIds.Add(TEXT("C2"));
			TestTrue(TEXT("Exact coin threshold"), Gadgets->CanUnlockGadget(Id));
			TestFalse(TEXT("Manual unlock forbidden outside Lab"), Gadgets->UnlockGadgetInLab(Id));
			World->SetGameMode(FURL(nullptr, TEXT("?game=/Script/REBoarGetch.BoarFacilityGameMode"), TRAVEL_Absolute));
			auto *Facility = World->GetAuthGameMode<ABoarFacilityGameMode>();
			if (TestNotNull(TEXT("Lab coordinator"), Facility))
			{
				TestTrue(TEXT("Lab permits coin-qualified unlock"), Gadgets->IsInGadgetLab());
				AddExpectedError(TEXT("\\[Save\\] Progress could not be saved\\."), EAutomationExpectedErrorFlags::Contains, 1);
				TestFalse(TEXT("Save failure rejects unlock"), Gadgets->UnlockGadgetInLab(Id));
				TestFalse(TEXT("Failed save does not publish unlock"), Gadgets->IsGadgetUnlocked(Id));
				TestEqual(TEXT("Unlock does not consume coins"), Gadgets->GetSpecialCoinCount(), 2);
				TestFalse(TEXT("Locked test selection rejected"), Gadgets->SelectTestGadget(Id));
				Instance->Progress->UnlockedGadgetIds.Add(Id);
				TArray<uint8> Before, After;
				UGameplayStatics::SaveGameToMemory(Instance->Progress, Before);
				TestTrue(TEXT("Unlocked gadget selected independently of slots"), Instance->SelectTestGadget(Id));
				TestEqual(TEXT("Selected test ID through facade"), Instance->GetSelectedTestGadget(), Id);
				TestTrue(TEXT("Test level resolved from definition"), Instance->GetSelectedTestLevel() == Definition->TestLevel);
				UGameplayStatics::SaveGameToMemory(Instance->Progress, After);
				TestTrue(TEXT("Trial selection leaves persistent save byte-identical"), Before == After);
				TestTrue(TEXT("None clears trial selection in Lab"), Instance->SelectTestGadget(NAME_None));
				TestTrue(TEXT("Cleared test has no level"), Instance->GetSelectedTestLevel().IsNull());
				Facility->bGadgetTest = true;
				TestFalse(TEXT("Test world cannot manually select"), Instance->SelectTestGadget(Id));
				Facility->bGadgetTest = false;
				Facility->bArchive = true;
				TestFalse(TEXT("Archive cannot manually select"), Instance->SelectTestGadget(Id));
				Facility->bArchive = false;
				Instance->SelectTestGadget(Id);
				Instance->ClearTestGadget();
				TestTrue(TEXT("Coordinator can clear test on travel"), Instance->GetSelectedTestGadget().IsNone());
				Instance->GadgetCatalog = {nullptr, NetClass, NetClass};
				auto *PC = World->SpawnActor<ABoarPlayerController>();
				auto *Player = World->SpawnActor<ABoarPlayerCharacter>();
				if (PC && Player)
				{
					PC->Possess(Player);
					auto* Component = Player->GetGadgetComponent();
					const TArray<TSubclassOf<AGadgetBase>> EmptySlots = {nullptr, nullptr, nullptr, nullptr};
					Component->InitializeResolvedLoadout(EmptySlots, Gadgets, true);
					TestTrue(TEXT("Temporary persistence policy supplied by coordinator"), Component->IsTemporaryLoadout());
					Component->InitializeResolvedLoadout(Defaults, Gadgets, false);
					TestTrue(TEXT("Initialization is idempotent and preserves temporary policy"), Component->IsTemporaryLoadout() && !Component->GetGadgetSlotClass(0));
					TArray<uint8> BeforeLoadout, AfterLoadout;
					UGameplayStatics::SaveGameToMemory(Instance->Progress, BeforeLoadout);
					TestTrue(TEXT("Temporary equip succeeds without persistence"), Component->SetGadgetSlot(0, NetClass));
					TestTrue(TEXT("Temporary equipment save result succeeds"), Component->WasLastLoadoutSaveSuccessful());
					TestTrue(TEXT("Duplicate gadget moves to another slot"), Component->SetGadgetSlot(2, NetClass));
					TestTrue(TEXT("Move clears old slot"), !Component->GetGadgetSlotClass(0) && Component->GetGadgetSlotClass(2) == NetClass);
					TestTrue(TEXT("Empty slot operation retained"), Component->SetGadgetSlot(2, nullptr));
					UGameplayStatics::SaveGameToMemory(Instance->Progress, AfterLoadout);
					TestTrue(TEXT("Temporary loadout leaves persistent save byte-identical"), BeforeLoadout == AfterLoadout);
					auto *Presenter = NewObject<UBoarLoadoutPresenter>();
					TestTrue(TEXT("Presenter connects in Lab"), Presenter->Initialize(PC, true));
					TestEqual(TEXT("Catalog keeps empty then unique gadget"), Presenter->GetCandidateCount(), 2);
					TestEqual(TEXT("Catalog display comes from definition"), Presenter->GetCandidate(1).Name.ToString(),
							  Definition->DisplayName.ToString());
					TestTrue(TEXT("Lab exposes test intent"), Presenter->ChooseLabMode(2));
					TestFalse(TEXT("Invalid mode rejected"), Presenter->ChooseLabMode(3));
					Presenter->ChooseCandidate(1);
					TestTrue(TEXT("Test candidate highlighted"), Presenter->GetCandidate(1).bSelected);
					Presenter->ChooseCandidate(0);
					TestTrue(TEXT("Empty candidate clears test"), Instance->GetSelectedTestGadget().IsNone());
					Presenter->Shutdown();
					TestTrue(TEXT("Pause connects without Lab operations"), Presenter->Initialize(PC, false));
					TestFalse(TEXT("Pause cannot request unlock mode"), Presenter->ChooseLabMode(1));
					TestFalse(TEXT("Pause cannot request trial mode"), Presenter->ChooseLabMode(2));
					TestEqual(TEXT("Invalid slot does not change selection"), Presenter->ChooseSlot(4), INDEX_NONE);
					Presenter->Shutdown();
				}
			}
		}
	}
	Instance->Shutdown();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
