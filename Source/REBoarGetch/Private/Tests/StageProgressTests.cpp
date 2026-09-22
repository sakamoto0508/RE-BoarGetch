#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Stage/StageRunData.h"
#include "Stage/StageConfig.h"
#include "BoarSaveGame.h"
#include "Boar/BoarBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Player/BoarPlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageInputLockTest, "REBoarGetch.Spec.StageInputLockOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStageInputLockTest::RunTest(const FString& Parameters)
{
	const auto Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("入力検証World"), World)) return false;
	ABoarPlayerController* PC = World->SpawnActor<ABoarPlayerController>();
	if (TestNotNull(TEXT("Controller"), PC))
	{
		PC->SetStageInputBlocked(true);
		PC->SetStageInputBlocked(true); // Stage停止→終了UIの順を再現します。
		PC->SetStageInputBlocked(false);
		TestFalse(TEXT("重複通知後も移動ロックが残らない"), PC->IsMoveInputIgnored());
		TestFalse(TEXT("重複通知後も視点ロックが残らない"), PC->IsLookInputIgnored());
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
		PC->SetStageInputBlocked(true);
		PC->SetStageInputBlocked(false);
		PC->SetStageInputBlocked(false);
		TestTrue(TEXT("他責務の移動ロックを解除しない"), PC->IsMoveInputIgnored());
		TestTrue(TEXT("他責務の視点ロックを解除しない"), PC->IsLookInputIgnored());
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageEndPriorityTest, "REBoarGetch.Spec.StageEndPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStageEndPriorityTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("同時要求はClear優先"), ResolveBoarStageEnd(EBoarStageState::Playing, true, true) == EBoarStageState::Cleared);
	TestTrue(TEXT("死亡のみはGameOver"), ResolveBoarStageEnd(EBoarStageState::Playing, false, true) == EBoarStageState::GameOver);
	TestTrue(TEXT("要求なしは継続"), ResolveBoarStageEnd(EBoarStageState::Playing, false, false) == EBoarStageState::Playing);
	TestTrue(TEXT("Clear後に死亡へ遷移しない"), ResolveBoarStageEnd(EBoarStageState::Cleared, false, true) == EBoarStageState::Cleared);
	TestTrue(TEXT("GameOver確定後の要求は無効"), ResolveBoarStageEnd(EBoarStageState::GameOver, true, false) == EBoarStageState::GameOver);
	TestTrue(TEXT("開始演出中は終了要求無効"), ResolveBoarStageEnd(EBoarStageState::Preparing, true, true) == EBoarStageState::Preparing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunProgressTest, "REBoarGetch.Spec.RunProgressAndSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRunProgressTest::RunTest(const FString& Parameters)
{
	UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
	Save->CapturedBoarUniqueIds.Add(TEXT("S1_B01"));
	Save->SpecialCoinIds.Add(TEXT("S1_C01"));
	Save->GadgetLoadout = {FName("Net"), NAME_None, FName("Sword"), NAME_None};
	Save->bHasSavedLoadout = true;
	Save->UnlockedGadgetIds.Add(TEXT("Net"));
	Save->LastAttemptedStageId = TEXT("S1");
	FStageRunData Run;
	Run.StageId = TEXT("S1");
	Run.CapturedBoarUniqueIds = {FName("S1_B01"), FName("S1_B02"), FName("S1_B02")};
	Run.SpecialCoinIds = {FName("S1_C01"), FName("S1_C02")};
	Run.CurrentHousedCount = 1;
	TestFalse(TEXT("未確定Runは保存不可"), Save->MergeClearedRun(Run));
	Save->FreezeRunResult(Run);
	TestEqual(TEXT("再捕獲・重複を除いたNEW"), Run.NewBoarUniqueIds.Num(), 1);
	TestTrue(TEXT("NEWは新個体のみ"), Run.NewBoarUniqueIds.Contains(TEXT("S1_B02")));
	TestEqual(TEXT("NEWコイン"), Run.NewSpecialCoinIds.Num(), 1);
	TestEqual(TEXT("NEW確定だけではSaveを変更しない"), Save->CapturedBoarUniqueIds.Num(), 1);
	TestTrue(TEXT("Clear候補へ反映"), Save->MergeClearedRun(Run));
	Save->FreezeRunResult(Run);
	TestEqual(TEXT("保存後もNEWを保持"), Run.NewBoarUniqueIds.Num(), 1);
	TestEqual(TEXT("現在収容数と履歴は別"), Run.CurrentHousedCount, 1);
	TestEqual(TEXT("保存個体は重複しない"), Save->CapturedBoarUniqueIds.Num(), 2);
	Run = FStageRunData();
	TestEqual(TEXT("挑戦破棄後も保存済み個体を保持"), Save->CapturedBoarUniqueIds.Num(), 2);
	TestEqual(TEXT("挑戦破棄後も装備4枠を保持"), Save->GadgetLoadout.Num(), 4);
	// 実ユーザーの保存スロットは使わず、メモリ上でUEのシリアライズを検証します。
	TArray<uint8> Bytes;
	TestTrue(TEXT("メモリ保存"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	UBoarSaveGame* Restored = Cast<UBoarSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("復元"), Restored)) return false;
	TestEqual(TEXT("固定IDを復元"), Restored->CapturedBoarUniqueIds.Num(), 2);
	TestTrue(TEXT("クリアStageを復元"), Restored->ClearedStageIds.Contains(TEXT("S1")));
	TestTrue(TEXT("空スロットを保持"), Restored->GadgetLoadout[1].IsNone());
	TestEqual(TEXT("最終Stageを復元"), Restored->LastAttemptedStageId, FName("S1"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageSpawnDefinitionTest, "REBoarGetch.Spec.SpawnDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStageSpawnDefinitionTest::RunTest(const FString& Parameters)
{
	UStageConfig* Config = NewObject<UStageConfig>();
	FBoarSpawnEntry Legacy;
	Legacy.BoarClass = ABoarBase::StaticClass();
	Legacy.Count = 5;
	Config->BoarSpawnEntries.Add(Legacy);
	Config->TargetCaptureCount = 3;
	TestEqual(TEXT("出現5/目標3は独立"), Config->GetTotalBoarSpawnCount(), 5);
	FBoarSpawnDefinition Individual;
	Individual.BoarClass = ABoarBase::StaticClass();
	Individual.BoarUniqueId = TEXT("S1_B01");
	Individual.SpawnPointId = TEXT("P1");
	Config->BoarSpawnDefinitions.Add(Individual);
	TestEqual(TEXT("個体定義は旧方式より優先"), Config->GetTotalBoarSpawnCount(), 1);
	TestEqual(TEXT("旧定義を消去しない"), Config->BoarSpawnEntries[0].Count, 5);
	return true;
}
#endif
