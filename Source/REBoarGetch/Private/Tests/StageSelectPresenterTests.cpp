#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UI/BoarStageSelectPresenter.h"
#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Stage/StageConfig.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageSelectPresenterTest, "REBoarGetch.Spec.StageSelectPresenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStageSelectPresenterTest::RunTest(const FString& Parameters)
{
	auto MakeStage = [](FName Id, FName Required = NAME_None)
	{
		auto* Stage = NewObject<UStageConfig>();
		Stage->StageId = Id;
		Stage->DisplayName = FText::FromName(Id);
		Stage->UnlockCondition.RequiredClearedStageId = Required;
		Stage->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Level/Stage1.Stage1")));
		return Stage;
	};
	auto* First = MakeStage(TEXT("S1"));
	auto* Locked = MakeStage(TEXT("S2"), TEXT("S1"));
	auto* Third = MakeStage(TEXT("S3"));
	auto* Duplicate = MakeStage(TEXT("S1"));
	auto* Invalid = MakeStage(NAME_None);
	auto* Presenter = NewObject<UBoarStageSelectPresenter>();
	Presenter->Initialize(nullptr, {nullptr, Invalid, First, Duplicate, Locked, Third}, Third);
	TestEqual(TEXT("Invalid and duplicate IDs excluded, original order preserved"), Presenter->GetStageCount(), 3);
	TestEqual(TEXT("Fallback object chosen"), Presenter->GetSelectedIndex(), 2);
	TestFalse(TEXT("Locked entry cannot be selected"), Presenter->TrySelect(1));
	TestEqual(TEXT("Rejected selection keeps current"), Presenter->GetSelectedIndex(), 2);
	TestTrue(TEXT("Unlocked entry can be selected"), Presenter->TrySelect(0));
	TestFalse(TEXT("Invalid index rejected"), Presenter->TrySelect(10));
	TestTrue(TEXT("List next skips locked entry"), Presenter->RequestStep(1, false));
	TestEqual(TEXT("List reaches third"), Presenter->GetSelectedIndex(), 2);
	Presenter->RequestStep(1, false);
	TestEqual(TEXT("List wraps to first"), Presenter->GetSelectedIndex(), 0);
	Presenter->RequestStep(-1, false);
	TestEqual(TEXT("List previous wraps"), Presenter->GetSelectedIndex(), 2);
	Presenter->Initialize(nullptr, {First, Locked, Third}, Duplicate);
	TestEqual(TEXT("Fallback outside deduplicated catalog uses first unlocked"), Presenter->GetSelectedIndex(), 0);
	TestTrue(TEXT("Carousel may browse locked adjacent card"), Presenter->RequestStep(1, true));
	TestEqual(TEXT("Animation keeps committed center until completion"), Presenter->GetSelectedIndex(), 0);
	TestNull(TEXT("No start during transition"), Presenter->RequestStart());
	Presenter->CommitPendingSelection();
	TestEqual(TEXT("Locked carousel card commits for display"), Presenter->GetSelectedIndex(), 1);
	TestFalse(TEXT("Locked view data"), Presenter->GetSelectedStage().bUnlocked);
	TestEqual(TEXT("Locked badge"), Presenter->GetSelectedStage().Badge.ToString(), FString(TEXT("LOCKED")));
	TestNull(TEXT("Locked carousel center never starts"), Presenter->RequestStart());
	TestTrue(TEXT("Next request accepted"), Presenter->RequestStep(1, true));
	TestTrue(TEXT("Rapid previous commits prior request before stepping"), Presenter->RequestStep(-1, true));
	TestEqual(TEXT("Rapid request commits third as intermediate center"), Presenter->GetSelectedIndex(), 2);
	Presenter->CommitPendingSelection();
	Presenter->RequestStep(-1, true); Presenter->CommitPendingSelection();
	TestFalse(TEXT("Carousel does not wrap at first"), Presenter->RequestStep(-1, true));
	TestTrue(TEXT("Visible preview IDs restricted to adjacent cards"), Presenter->IsPreviewVisible(TEXT("S2")));
	TestFalse(TEXT("Distant preview released"), Presenter->IsPreviewVisible(TEXT("S3")));
	TestFalse(TEXT("Unknown preview absent"), Presenter->IsPreviewVisible(TEXT("Missing")));
	Presenter->Initialize(nullptr, {Locked}, Locked);
	TestEqual(TEXT("No usable stage leaves selection empty"), Presenter->GetSelectedIndex(), INDEX_NONE);
	TestFalse(TEXT("No unlocked list step"), Presenter->RequestStep(1, false));
	TestNull(TEXT("No usable stage cannot start"), Presenter->RequestStart());
	Presenter->Initialize(nullptr, {}, First);
	TestEqual(TEXT("Empty catalog does not insert fallback"), Presenter->GetStageCount(), 0);
	TestFalse(TEXT("Empty catalog step is safe"), Presenter->RequestStep(1, false));

	// GUID slot isolates this fixture; only the notification checks write, and cleanup deletes it.
	auto* Instance = NewObject<UBoarGameInstance>(GEngine);
	auto* SlotProperty = FindFProperty<FStrProperty>(UBoarGameInstance::StaticClass(), TEXT("SaveSlotName"));
	if (!TestNotNull(TEXT("Existing reflected save slot setting"), SlotProperty)) return false;
	const FString Slot = TEXT("REBoarGetch_StageSelectTest_") + FGuid::NewGuid().ToString();
	SlotProperty->SetPropertyValue_InContainer(Instance, Slot);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	ON_SCOPE_EXIT
	{
		Presenter->Shutdown();
		Instance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
	};
	auto* Save = const_cast<UBoarSaveGame*>(Instance->GetProgress());
	Save->LastAttemptedStageId = TEXT("S3");
	Presenter->Initialize(Instance, {First, Locked, Third}, First);
	TestEqual(TEXT("Valid last attempted takes priority over fallback"), Presenter->GetSelectedIndex(), 2);
	Save->LastAttemptedStageId = TEXT("S2");
	Presenter->Initialize(Instance, {First, Locked, Third}, Third);
	TestEqual(TEXT("Locked last attempted uses fallback"), Presenter->GetSelectedIndex(), 2);
	Save->LastAttemptedStageId = TEXT("Missing");
	Presenter->Initialize(Instance, {First, Locked, Third}, nullptr);
	TestEqual(TEXT("Unknown last attempted uses first unlocked"), Presenter->GetSelectedIndex(), 0);
	First->DisplayName = FText::GetEmpty();
	First->TargetCaptureCount = 4;
	FSpecialCoinDefinition Coin; Coin.SpecialCoinId = TEXT("C1"); First->SpecialCoinDefinitions.Add(Coin);
	First->SpecialCoinDefinitions.Add(Coin); Coin.SpecialCoinId = TEXT("C2"); First->SpecialCoinDefinitions.Add(Coin);
	Coin.SpecialCoinId = NAME_None; First->SpecialCoinDefinitions.Add(Coin);
	FBoarSpawnDefinition Boar; Boar.BoarUniqueId = TEXT("B1"); First->BoarSpawnDefinitions.Add(Boar);
	First->BoarSpawnDefinitions.Add(Boar); Boar.BoarUniqueId = TEXT("B2"); First->BoarSpawnDefinitions.Add(Boar);
	Boar.BoarUniqueId = NAME_None; First->BoarSpawnDefinitions.Add(Boar);
	Save->SpecialCoinIds = {TEXT("C1"), TEXT("OtherStageCoin")};
	Save->CapturedBoarUniqueIds = {TEXT("B2"), TEXT("OtherStageBoar")};
	Save->ClearedStageIds.Add(TEXT("S1"));
	TArray<uint8> Before, After;
	UGameplayStatics::SaveGameToMemory(Save, Before);
	const auto Data = Presenter->GetSelectedStage();
	TestTrue(TEXT("Cleared flag"), Data.bCleared);
	TestEqual(TEXT("Clear status"), Data.Status.ToString(), FString(TEXT("CLEAR")));
	TestEqual(TEXT("Deduplicated stage-local coin and boar progress"), Data.Progress.ToString(), FString(TEXT("特別コイン：1 / 2\n図鑑：1 / 2")));
	TestTrue(TEXT("Empty entry display name uses stage ID"), Data.EntryLabel.ToString().StartsWith(TEXT("S1\n")));
	TestTrue(TEXT("Detail display name retains original empty text"), Data.DisplayName.IsEmpty());
	TestEqual(TEXT("Capture target kept independent of collection count"), Data.TargetCaptureCount, 4);
	TestTrue(TEXT("Clear unlocks dependent stage"), Presenter->TrySelect(1));
	TestTrue(TEXT("Valid start returns exact catalog object"), Presenter->RequestStart() == Locked);
	Locked->Level.Reset();
	TestNull(TEXT("Missing level blocks start"), Presenter->RequestStart());
	TestEqual(TEXT("Missing level status takes priority"), Presenter->GetSelectedStage().Status.ToString(), FString(TEXT("出発先未登録")));
	TestEqual(TEXT("Legacy/no-individual stage progress"), Presenter->GetSelectedStage().Progress.ToString(), FString(TEXT("特別コイン：0 / 0\n図鑑：個体データ未登録")));
	UGameplayStatics::SaveGameToMemory(Save, After);
	TestTrue(TEXT("Selection and view-data reads leave save byte-identical"), Before == After);
	TestTrue(TEXT("Presenter subscribed to model"), Instance->OnProgressChanged.IsBoundToObject(Presenter));
	bool bPublishedBeforeNotification = false;
	const auto Handle = Instance->OnProgressChanged.AddLambda([&] { bPublishedBeforeNotification = Presenter->GetStage(2).bCleared; });
	FStageRunData Run; Run.StageId = TEXT("S3");
	TestTrue(TEXT("Isolated save commit succeeds"), Instance->CommitClearedRun(Run));
	TestTrue(TEXT("Model notification reads published candidate"), bPublishedBeforeNotification);
	Instance->OnProgressChanged.Remove(Handle);
	const auto* Published = Instance->GetProgress();
	bool bNotifiedOnFailure = false;
	const auto FailureHandle = Instance->OnProgressChanged.AddLambda([&] { bNotifiedOnFailure = true; });
	SlotProperty->SetPropertyValue_InContainer(Instance, FString());
	AddExpectedError(TEXT("\\[Save\\] Progress could not be saved\\."), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Failed save rejected"), Instance->SaveLastAttemptedStage(TEXT("S1")));
	TestFalse(TEXT("Failed save sends no progress notification"), bNotifiedOnFailure);
	TestTrue(TEXT("Failed save keeps published instance"), Instance->GetProgress() == Published);
	SlotProperty->SetPropertyValue_InContainer(Instance, Slot);
	Instance->OnProgressChanged.Remove(FailureHandle);
	Presenter->Shutdown();
	TestFalse(TEXT("Closing presenter removes model subscription"), Instance->OnProgressChanged.IsBoundToObject(Presenter));
	TestNull(TEXT("Closed presenter cannot start"), Presenter->RequestStart());
	return true;
}
#endif
