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
 TestFalse(TEXT("Duplicate individual rejected"),Save->SetArchiveDisplays({TEXT("A"),TEXT("A"),NAME_None}));
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
 TestTrue(TEXT("Assign an already displayed Boar swaps slots"),Save->AssignArchiveDisplay(2,TEXT("A")));
 TestEqual(TEXT("Old active Boar moves to source slot"),Save->ResolveArchiveDisplays()[0],FName(TEXT("C")));
 TestEqual(TEXT("Selected Boar moves to active slot"),Save->ResolveArchiveDisplays()[2],FName(TEXT("A")));
 TestFalse(TEXT("Uncaptured assignment rejected"),Save->AssignArchiveDisplay(0,TEXT("X")));
 TestFalse(TEXT("Invalid slot rejected"),Save->AssignArchiveDisplay(3,TEXT("A")));
 for(int32 I=0;I<3;++I)TestTrue(TEXT("Explicitly clear each slot"),Save->AssignArchiveDisplay(I,NAME_None));
 Run.CapturedBoarUniqueIds={TEXT("D")};Save->MergeClearedRun(Run);
 TestTrue(TEXT("Empty custom archive does not revert to default"),Save->ResolveArchiveDisplays()[0].IsNone()&&Save->ResolveArchiveDisplays()[1].IsNone()&&Save->ResolveArchiveDisplays()[2].IsNone());
 TArray<uint8> EmptyBytes;UGameplayStatics::SaveGameToMemory(Save,EmptyBytes);
 auto* EmptyLoaded=Cast<UBoarSaveGame>(UGameplayStatics::LoadGameFromMemory(EmptyBytes));
 TestTrue(TEXT("All empty custom slots survive reload"),EmptyLoaded&&EmptyLoaded->bHasSavedArchiveDisplays&&EmptyLoaded->ResolveArchiveDisplays()[0].IsNone());
 return true;
}
#endif
