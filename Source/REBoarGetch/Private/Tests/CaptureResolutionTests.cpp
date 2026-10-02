#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/BoarCaptureResolution.h"
#include "Cage/Cage.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarCaptureDestinationTest, "REBoarGetch.Spec.CaptureDestination",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoarCaptureDestinationTest::RunTest(const FString &Parameters)
{
	const auto Initialization =
		UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Capture test world"), World))
		return false;
	TestNull(TEXT("No cage has no destination"), BoarCaptureResolution::FindDestination(World, FVector::ZeroVector));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Name = TEXT("Cage_B");
	auto *Near = World->SpawnActor<ACage>(FVector(100, 0, 0), FRotator::ZeroRotator, Params);
	Params.Name = TEXT("Cage_A");
	auto *Far = World->SpawnActor<ACage>(FVector(200, 0, 0), FRotator::ZeroRotator, Params);
	if (TestNotNull(TEXT("Near cage"), Near) && TestNotNull(TEXT("Far cage"), Far))
	{
		Near->InitializeForStage();
		Far->InitializeForStage();
		TestTrue(TEXT("Nearest healthy destination"), BoarCaptureResolution::FindDestination(World, FVector::ZeroVector) == Near);
		Far->SetActorLocation(FVector(-100, 0, 0));
		TestTrue(TEXT("Equal distance uses stable path order"), BoarCaptureResolution::FindDestination(World, FVector::ZeroVector) == Far);
		Far->SetActorLocation(FVector(200, 0, 0));
		Near->ApplyDamage(Near->GetMaxHP());
		TestTrue(TEXT("Near cage destroyed via gameplay API"), Near->GetIsCageDestroyed());
		TestTrue(TEXT("Healthy cage takes precedence over nearer destroyed cage"),
				 BoarCaptureResolution::FindDestination(World, FVector::ZeroVector) == Far);
		Far->ApplyDamage(Far->GetMaxHP());
		TestTrue(TEXT("Nearest destroyed cage is fallback"), BoarCaptureResolution::FindDestination(World, FVector::ZeroVector) == Near);
	}
	World->DestroyWorld(false);
	return true;
}
#endif
