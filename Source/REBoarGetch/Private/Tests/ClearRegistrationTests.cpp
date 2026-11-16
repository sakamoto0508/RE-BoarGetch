#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Save/BoarSaveGame.h"
#include "Kismet/GameplayStatics.h"

// AKI-45：Clear時の収容個体だけを登録・NEW・最近捕獲順の対象にする（SaveGame単体のデータ検証。PIEは不要）。
namespace
{
	FStageRunData MakeClearRun(const TArray<FName>& History, const TMap<FName, int32>& Orders, const TArray<FName>& Housed)
	{
		FStageRunData Run;
		Run.StageId = TEXT("S1");
		Run.CapturedBoarUniqueIds = History;
		Run.LastCaptureOrderByBoar = Orders;
		Run.HousedBoarUniqueIds = Housed;
		Run.bHousedSnapshotTaken = true;
		return Run;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClearRegistrationTest, "REBoarGetch.Spec.ClearHousedRegistration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearRegistrationTest::RunTest(const FString& Parameters)
{
	// B：未登録Aを捕獲→解放→別のBでClear。Aは履歴にあるが未収容。
	{
		UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
		FStageRunData Run = MakeClearRun({"A", "B"}, {{"A", 1}, {"B", 2}}, {"B"});
		Save->FreezeRunResult(Run);
		TestEqual(TEXT("B: 解放されたAはNEWにならない"), Run.NewBoarUniqueIds, TArray<FName>{"B"});
		TestTrue(TEXT("B: 保存できる"), Save->MergeClearedRun(Run));
		TestFalse(TEXT("B: 解放されたAは登録しない"), Save->CapturedBoarUniqueIds.Contains("A"));
		TestTrue(TEXT("B: 収容中のBは登録"), Save->CapturedBoarUniqueIds.Contains("B"));
		TestFalse(TEXT("B: Aは最近捕獲順にも入らない"), Save->CapturedBoarHistory.Contains("A"));
	}
	// C：登録済みAを再捕獲してClear。NEWにはならないが、最近捕獲順の先頭へ移る。未収容のXの登録と順序は維持。
	{
		UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
		Save->CapturedBoarUniqueIds = {"A", "X", "Y"};
		Save->CapturedBoarHistory = {"A", "X", "Y"};
		FStageRunData Run = MakeClearRun({"A"}, {{"A", 1}}, {"A"});
		Save->FreezeRunResult(Run);
		TestTrue(TEXT("C: 再捕獲はNEWではない"), Run.NewBoarUniqueIds.IsEmpty());
		Save->MergeClearedRun(Run);
		TestEqual(TEXT("C: 最近捕獲順を更新"), Save->GetRecentCapturedBoarIds(), TArray<FName>{"A", "Y", "X"});
		TestEqual(TEXT("C: 登録数は増えない"), Save->CapturedBoarUniqueIds.Num(), 3);
	}
	// D：同じ一振り（同じ順序番号）でC・Aを捕獲してClear。新しい順に読むとID昇順。
	{
		UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
		Save->CapturedBoarUniqueIds = {"Z"};
		Save->CapturedBoarHistory = {"Z"};
		FStageRunData Run = MakeClearRun({"C", "A"}, {{"C", 1}, {"A", 1}}, {"A", "C"});
		Save->FreezeRunResult(Run);
		TestEqual(TEXT("D: NEWもID昇順"), Run.NewBoarUniqueIds, TArray<FName>{"A", "C"});
		Save->MergeClearedRun(Run);
		TestEqual(TEXT("D: 同時捕獲はID昇順、過去の保存より新しい"), Save->GetRecentCapturedBoarIds(), TArray<FName>{"A", "C", "Z"});
		TestEqual(TEXT("D: Default展示"), Save->ResolveArchiveDisplays(), TArray<FName>{"A", "C", "Z"});
	}
	// E：A捕獲→解放→Bを捕獲→Aを再捕獲・収容してClear。重複登録せず、Aが最新。
	{
		UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
		FStageRunData Run = MakeClearRun({"A", "B"}, {{"A", 3}, {"B", 2}}, {"A", "B"});
		Save->FreezeRunResult(Run);
		TestEqual(TEXT("E: NEWは新しい順"), Run.NewBoarUniqueIds, TArray<FName>{"A", "B"});
		Save->MergeClearedRun(Run);
		TestEqual(TEXT("E: 再捕獲したAが最新"), Save->GetRecentCapturedBoarIds(), TArray<FName>{"A", "B"});
		TestEqual(TEXT("E: 重複なし"), Save->CapturedBoarHistory.Num(), 2);
	}
	// 収容スナップショットの無いRun（Clear処理を経ていない）からは個体を登録しない。
	{
		UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
		FStageRunData Run;
		Run.StageId = TEXT("S1");
		Run.CapturedBoarUniqueIds = {"A"};
		Save->FreezeRunResult(Run);
		TestTrue(TEXT("スナップショット無しはNEW無し"), Run.NewBoarUniqueIds.IsEmpty());
		Save->MergeClearedRun(Run);
		TestFalse(TEXT("スナップショット無しは登録しない"), Save->CapturedBoarUniqueIds.Contains("A"));
	}
	// G：順序情報の無い旧セーブ。登録を消さず、履歴のある個体の後ろにID昇順で並べる。
	{
		UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
		Save->CapturedBoarUniqueIds = {"M", "B", "K"};
		Save->CapturedBoarHistory = {"K"};
		TestEqual(TEXT("G: 旧データの並び"), Save->GetRecentCapturedBoarIds(), TArray<FName>{"K", "B", "M"});
		FStageRunData Run = MakeClearRun({"Q"}, {{"Q", 1}}, {"Q"});
		Save->FreezeRunResult(Run);
		Save->MergeClearedRun(Run);
		TArray<uint8> Bytes;
		TestTrue(TEXT("G: メモリ保存"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
		const UBoarSaveGame* Loaded = Cast<UBoarSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (TestNotNull(TEXT("G: 復元"), Loaded))
		{
			TestEqual(TEXT("G: 再起動後も同じ並び"), Loaded->GetRecentCapturedBoarIds(), TArray<FName>{"Q", "K", "B", "M"});
			TestEqual(TEXT("G: 登録を失わない"), Loaded->CapturedBoarUniqueIds.Num(), 4);
		}
	}
	// 旧セーブと新しいClearの混在：順序の無い旧個体は最近捕獲した個体より新しくならない。
	// 旧個体Bを再捕獲・収容してClearすると、そのときだけ最新になる。Custom展示は変わらない。
	{
		UBoarSaveGame* Save = NewObject<UBoarSaveGame>();
		Save->CapturedBoarUniqueIds = {"B", "M"};
		FStageRunData First = MakeClearRun({"Z"}, {{"Z", 1}}, {"Z"});
		Save->FreezeRunResult(First);
		Save->MergeClearedRun(First);
		TestEqual(TEXT("旧個体は新しいClearより古い"), Save->GetRecentCapturedBoarIds(), TArray<FName>{"Z", "B", "M"});
		TestTrue(TEXT("Custom展示を保存"), Save->SetArchiveDisplays({"M", NAME_None, "Z"}));
		FStageRunData Second = MakeClearRun({"B"}, {{"B", 1}}, {"B"});
		Save->FreezeRunResult(Second);
		TestTrue(TEXT("旧個体の再捕獲はNEWではない"), Second.NewBoarUniqueIds.IsEmpty());
		Save->MergeClearedRun(Second);
		TestEqual(TEXT("再捕獲した旧個体が最新"), Save->GetRecentCapturedBoarIds(), TArray<FName>{"B", "Z", "M"});
		TestEqual(TEXT("Custom展示は捕獲順で変わらない"), Save->ResolveArchiveDisplays(), TArray<FName>{"M", NAME_None, "Z"});
	}
	return true;
}
#endif
