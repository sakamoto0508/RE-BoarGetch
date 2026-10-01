#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Save/BoarSaveGame.h"
#include "Kismet/GameplayStatics.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchiveDisplaysTest,"REBoarGetch.Spec.ArchiveDisplays",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArchiveDisplaysTest::RunTest(const FString& Parameters)
{
 auto* Save=NewObject<UBoarSaveGame>();
 TestEqual(TEXT("Empty archive has three slots"),Save->ResolveArchiveDisplays().Num(),3);
 FStageRunData Run;Run.StageId=TEXT("Stage1");Run.bResultFrozen=true;
 Run.CapturedBoarUniqueIds={TEXT("A"),TEXT("B"),TEXT("C"),TEXT("D")};
 Save->MergeClearedRun(Run);
 auto Ids=Save->ResolveArchiveDisplays();
 TestEqual(TEXT("Latest first"),Ids[0],FName(TEXT("D")));
 TestEqual(TEXT("Third latest"),Ids[2],FName(TEXT("B")));
 TestFalse(TEXT("Uncaptured selection rejected"),Save->SetArchiveDisplays({TEXT("X"),NAME_None,NAME_None}));
 TestFalse(TEXT("Wrong slot count rejected"),Save->SetArchiveDisplays({TEXT("A")}));
 TestTrue(TEXT("Captured IDs and empties accepted"),Save->SetArchiveDisplays({TEXT("A"),NAME_None,TEXT("C")}));
 Run.CapturedBoarUniqueIds={TEXT("B")};Save->MergeClearedRun(Run);
 TestEqual(TEXT("Saved selection takes priority"),Save->ResolveArchiveDisplays()[0],FName(TEXT("A")));
 TArray<uint8> Bytes;
 TestTrue(TEXT("Save serializes"),UGameplayStatics::SaveGameToMemory(Save,Bytes));
 auto* Loaded=Cast<UBoarSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
 if(TestNotNull(TEXT("Save reloads"),Loaded))
 {
  TestTrue(TEXT("Explicit selection persists"),Loaded->bHasSavedArchiveDisplays);
  TestEqual(TEXT("Empty middle slot persists"),Loaded->ResolveArchiveDisplays()[1],NAME_None);
  TestEqual(TEXT("History persists"),Loaded->CapturedBoarHistory.Last(),FName(TEXT("B")));
 }
 return true;
}
#endif
