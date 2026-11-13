#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/ACFCameraTestEnvironment.h"
#include "ACFCameraHealth.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

/**
 * Phase 6B / B-9: the test environment itself. The scenario runners rely on FGlobalStateGuard to restore what a test
 * changed (also when it aborts), and on local-player teardown to unbind the subsystem from the static World delegates.
 * Game process only (a real game World): README 22 章.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTestEnvironmentGlobalStateTest, "ActionCameraFramework.TestEnvironment.GlobalStateIsRestored",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFTestEnvironmentGlobalStateTest::RunTest(const FString& Parameters)
{
	using namespace ACFTestEnvironment;
	UWorld* World = FindGameWorld();
	if (!TestNotNull(TEXT("Game World"), World) || !TestNotNull(TEXT("World Settings"), World->GetWorldSettings()))
	{
		return false;
	}
	{
		FGlobalStateGuard Untouched;
		Untouched.Capture(World);
		TestEqual(TEXT("Nothing changed: nothing to restore"), Untouched.Restore().Num(), 0);
	}

	IConsoleVariable* Solver = IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Debug.Solver"));
	UACFCameraSettings* Settings = GetMutableDefault<UACFCameraSettings>();
	const FString SolverBefore = Solver ? Solver->GetString() : FString();
	const bool bGuardBefore = Settings->bEnableOutputCollisionGuard;
	const bool bFixedBefore = FApp::UseFixedTimeStep();
	const bool bPausedBefore = UGameplayStatics::IsGamePaused(World);
	const float DilationBefore = World->GetWorldSettings()->TimeDilation;

	FGlobalStateGuard Guard;
	Guard.Capture(World);
	int32 Changed = 0;
	if (Solver)
	{
		Solver->Set(Solver->GetInt() == 0 ? 1 : 0, ECVF_SetByCode);
		++Changed;
	}
	Settings->bEnableOutputCollisionGuard = !bGuardBefore;
	FApp::SetUseFixedTimeStep(!bFixedBefore);
	UGameplayStatics::SetGamePaused(World, !bPausedBefore);
	World->GetWorldSettings()->SetTimeDilation(DilationBefore * 0.5f);
	Changed += 4;

	const TArray<FString> Restored = Guard.Restore();
	AddInfo(FString::Printf(TEXT("Restored: %s"), *FString::Join(Restored, TEXT(", "))));
	TestEqual(TEXT("Every change is named"), Restored.Num(), Changed);
	TestEqual(TEXT("Console variable restored"), Solver ? Solver->GetString() : FString(), SolverBefore);
	TestEqual(TEXT("Settings restored (output collision guard)"), Settings->bEnableOutputCollisionGuard, bGuardBefore);
	TestEqual(TEXT("Fixed time step restored"), FApp::UseFixedTimeStep(), bFixedBefore);
	TestEqual(TEXT("Pause restored"), UGameplayStatics::IsGamePaused(World), bPausedBefore);
	TestEqual(TEXT("Time dilation restored"), World->GetWorldSettings()->TimeDilation, DilationBefore);
	TestEqual(TEXT("A second restore does nothing"), Guard.Restore().Num(), 0);

	// Diagnostics of the real player start from zero for each scenario.
	const APlayerController* PC = World->GetFirstPlayerController();
	UACFCameraSubsystem* Acf = PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (TestNotNull(TEXT("ACF subsystem of the local player"), Acf))
	{
		BeginScenario(*Acf); // from zero, so the report below is occurrence 1 and logs exactly once
		AddExpectedMessagePlain(TEXT("B9EnvironmentProbe"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		Acf->GetDirector()->GetHealth().Report(EACFCameraHealthIssue::InvalidTarget, NAME_None, TEXT("B9EnvironmentProbe"), TEXT("none"));
		TestTrue(TEXT("Reported"), Acf->GetHealthDiagnostics().TotalIssues > 0);
		BeginScenario(*Acf);
		TestEqual(TEXT("BeginScenario: health counters back to zero"), Acf->GetHealthDiagnostics().TotalIssues, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTestEnvironmentDelegatesTest, "ActionCameraFramework.TestEnvironment.LocalPlayerTeardownUnbindsWorldDelegates",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFTestEnvironmentDelegatesTest::RunTest(const FString& Parameters)
{
	using namespace ACFTestEnvironment;
	UWorld* World = FindGameWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!TestNotNull(TEXT("Game instance"), GameInstance))
	{
		return false;
	}
	ExpectHostWidgetErrorForExtraLocalPlayers(*this);
	FString Error;
	ULocalPlayer* Extra = GameInstance->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(7), Error, true);
	if (!TestNotNull(FString::Printf(TEXT("Extra local player (%s)"), *Error), Extra))
	{
		return false;
	}
	const UACFCameraSubsystem* Acf = Extra->GetSubsystem<UACFCameraSubsystem>();
	TestNotNull(TEXT("Its ACF subsystem"), Acf);
	TestTrue(TEXT("While the player exists: bound to OnWorldTickStart"), FWorldDelegates::OnWorldTickStart.IsBoundToObject(Acf));
	TestTrue(TEXT("While the player exists: bound to OnWorldPostActorTick"), FWorldDelegates::OnWorldPostActorTick.IsBoundToObject(Acf));
	GameInstance->RemoveLocalPlayer(Extra);
	// Only the address is compared: the subsystem object may already be pending kill.
	TestFalse(TEXT("After removing the player: OnWorldTickStart unbound"), FWorldDelegates::OnWorldTickStart.IsBoundToObject(Acf));
	TestFalse(TEXT("After removing the player: OnWorldPostActorTick unbound"), FWorldDelegates::OnWorldPostActorTick.IsBoundToObject(Acf));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
