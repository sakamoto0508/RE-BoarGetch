#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "ACFCameraTestUtils.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDirectorInvalidInputTest, "ActionCameraFramework.Director.InvalidInput", ACFTests::Flags)
bool FACFDirectorInvalidInputTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	AddExpectedError(TEXT("preset is null"), EAutomationExpectedErrorFlags::Contains, 1);

	const FACFCameraRequestHandle Handle = Director->AddRequest(nullptr, FACFCameraRequestParams());
	TestFalse(TEXT("Null preset yields invalid handle"), Handle.IsValid());
	TestFalse(TEXT("Releasing invalid handle is a no-op"), Director->RemoveRequest(Handle));
	TestFalse(TEXT("Releasing unknown handle is a no-op"), Director->RemoveRequest(FACFCameraRequestHandle(987654)));

	ACFTests::FFrameDriver Driver;
	const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), 0.016f);
	TestEqual(TEXT("No requests => no influence"), Result.Weight, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDirectorFixedOutputTest, "ActionCameraFramework.Director.FixedOutput", ACFTests::Flags)
bool FACFDirectorFixedOutputTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Preset(ACFTests::MakeFixedPreset(FVector(100, 200, 300), 60.f));

	FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FACFCameraRequestParams());
	TestTrue(TEXT("Handle valid"), Handle.IsValid());

	ACFTests::FFrameDriver Driver;
	Driver.Upstream.Location = FVector(-5, -5, -5);
	const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), 0.016f);
	TestEqual(TEXT("Cut => full weight"), Result.Weight, 1.f);
	TestEqual(TEXT("Fixed location"), Result.Pose.Location, FVector(100, 200, 300));
	TestTrue(TEXT("Fixed rotation"), Result.Pose.Rotation.Equals(FRotator(-10.f, 45.f, 0.f), 0.01f));
	TestEqual(TEXT("Fixed FOV"), Result.Pose.FieldOfView, 60.f);
	TestTrue(TEXT("Active handle reported"), Result.ActiveRequest == Handle);

	TestTrue(TEXT("Release succeeds"), Director->RemoveRequest(Handle));
	TestFalse(TEXT("Double release is a no-op"), Director->RemoveRequest(Handle));
	const FACFCameraEvaluationResult& After = Driver.Step(Director.Get(), 0.016f);
	TestEqual(TEXT("Back to UE camera"), After.Weight, 0.f);
	TestFalse(TEXT("No active request"), After.ActiveRequest.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDirectorPriorityTest, "ActionCameraFramework.Director.Priority", ACFTests::Flags)
bool FACFDirectorPriorityTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> PresetA(ACFTests::MakeFixedPreset(FVector(1, 0, 0), 90.f));
	TStrongObjectPtr<UACFCameraPreset> PresetB(ACFTests::MakeFixedPreset(FVector(2, 0, 0), 90.f));
	TStrongObjectPtr<UACFCameraPreset> PresetC(ACFTests::MakeFixedPreset(FVector(3, 0, 0), 90.f));
	ACFTests::FFrameDriver Driver;

	FACFCameraRequestParams High;
	High.Priority = 10;
	FACFCameraRequestParams Low;
	Low.Priority = 0;

	const FACFCameraRequestHandle HighHandle = Director->AddRequest(PresetA.Get(), High);
	const FACFCameraRequestHandle LowHandle = Director->AddRequest(PresetB.Get(), Low);
	TestEqual(TEXT("Higher priority wins even if older"), Driver.Step(Director.Get(), 0.016f).Pose.Location.X, 1.0);

	const FACFCameraRequestHandle SameHandle = Director->AddRequest(PresetC.Get(), High);
	TestEqual(TEXT("Equal priority: newest wins"), Driver.Step(Director.Get(), 0.016f).Pose.Location.X, 3.0);

	Director->RemoveRequest(SameHandle);
	TestEqual(TEXT("Release returns to previous equal-priority request"), Driver.Step(Director.Get(), 0.016f).Pose.Location.X, 1.0);

	Director->RemoveRequest(HighHandle);
	const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), 0.016f);
	TestEqual(TEXT("Falls back to low priority"), Result.Pose.Location.X, 2.0);
	TestTrue(TEXT("Low is active"), Result.ActiveRequest == LowHandle);

	// Releasing an inactive request must not disturb the active one.
	FACFCameraRequestParams LowestParams;
	LowestParams.Priority = -5;
	const FACFCameraRequestHandle Hidden = Director->AddRequest(PresetA.Get(), LowestParams);
	Driver.Step(Director.Get(), 0.016f);
	Director->RemoveRequest(Hidden);
	TestTrue(TEXT("Inactive release keeps active"), Driver.Step(Director.Get(), 0.016f).ActiveRequest == LowHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDirectorBlendTest, "ActionCameraFramework.Director.BlendAndFrameGuard", ACFTests::Flags)
bool FACFDirectorBlendTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> PresetA(ACFTests::MakeFixedPreset(FVector(0, 0, 0), 90.f, ACFTests::Linear(1.f)));
	TStrongObjectPtr<UACFCameraPreset> PresetB(ACFTests::MakeFixedPreset(FVector(100, 0, 0), 50.f, ACFTests::Linear(1.f)));
	ACFTests::FFrameDriver Driver;

	FACFCameraRequestHandle A = Director->AddRequest(PresetA.Get(), FACFCameraRequestParams());
	TestTrue(TEXT("Blend-in from UE starts at weight 0.25"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 0.25f).Weight, 0.25f, 1e-4f));

	// The PCM evaluates modifiers again for its pending view target in the same frame.
	TestTrue(TEXT("Same-frame call does not advance the blend"), FMath::IsNearlyEqual(Driver.Repeat(Director.Get(), 0.25f).Weight, 0.25f, 1e-4f));

	Driver.Step(Director.Get(), 1.f);
	TestEqual(TEXT("Blend-in completes"), Director->GetLastResult().Weight, 1.f);

	// A -> B over 1s linear.
	Director->AddRequest(PresetB.Get(), FACFCameraRequestParams());
	const FACFCameraEvaluationResult& Half = Driver.Step(Director.Get(), 0.5f);
	TestTrue(TEXT("Request-to-request blend halfway"), FMath::IsNearlyEqual(Half.Pose.Location.X, 50.0, 1e-3));
	TestTrue(TEXT("FOV blends too"), FMath::IsNearlyEqual(Half.Pose.FieldOfView, 70.f, 1e-3f));
	const double MidX = Half.Pose.Location.X;

	// Interrupting the blend (release B, A takes over) must continue from the current pose.
	Director->RemoveAllRequests(false);
	A = Director->AddRequest(PresetA.Get(), FACFCameraRequestParams());
	const FACFCameraEvaluationResult& Interrupted = Driver.Step(Director.Get(), 0.f);
	TestTrue(TEXT("No pop on interruption"), FMath::IsNearlyEqual(Interrupted.Pose.Location.X, MidX, 1e-3));

	// Zero-delta evaluation followed by a real one in the same frame advances exactly once.
	const FACFCameraEvaluationResult& Upgraded = Driver.Repeat(Director.Get(), 0.5f);
	TestTrue(TEXT("Zero-dt then real-dt advances once"), FMath::IsNearlyEqual(Upgraded.Pose.Location.X, MidX * 0.5, 1e-3));

	// Blend-out back to UE uses the released preset's blend-out.
	Director->RemoveRequest(A);
	TestTrue(TEXT("Blend-out halfway"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 0.5f).Weight, 0.5f, 1e-4f));
	TestEqual(TEXT("Blend-out completes"), Driver.Step(Director.Get(), 0.6f).Weight, 0.f);

	// Immediate release-all cuts.
	Director->AddRequest(PresetA.Get(), FACFCameraRequestParams());
	Driver.Step(Director.Get(), 0.5f);
	Director->RemoveAllRequests(true);
	TestEqual(TEXT("Immediate release-all cuts to UE"), Driver.Step(Director.Get(), 0.016f).Weight, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDirectorInstigatorTest, "ActionCameraFramework.Director.InstigatorLifetime", ACFTests::Flags)
bool FACFDirectorInstigatorTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Preset(ACFTests::MakeFixedPreset(FVector(1, 2, 3), 90.f));
	ACFTests::FFrameDriver Driver;

	UACFCameraPreset* Instigator = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	FACFCameraRequestParams Params;
	Params.Instigator = Instigator;
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), Params);
	TestTrue(TEXT("Active while instigator alive"), Driver.Step(Director.Get(), 0.016f).ActiveRequest == Handle);

	Instigator->MarkAsGarbage();
	const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), 0.016f);
	TestFalse(TEXT("Request auto-released"), Director->HasRequest(Handle));
	TestEqual(TEXT("Back to UE (cut blend-out)"), Result.Weight, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDirectorIsolationTest, "ActionCameraFramework.Director.PerPlayerIsolation", ACFTests::Flags)
bool FACFDirectorIsolationTest::RunTest(const FString& Parameters)
{
	// Two local players share one preset; their blend state must stay independent.
	TStrongObjectPtr<UACFCameraDirector> Player1(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> Player2(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Shared(ACFTests::MakeFixedPreset(FVector(10, 0, 0), 90.f, ACFTests::Linear(1.f)));
	ACFTests::FFrameDriver Driver1;
	ACFTests::FFrameDriver Driver2;

	const FACFCameraRequestHandle H1 = Player1->AddRequest(Shared.Get(), FACFCameraRequestParams());
	const FACFCameraRequestHandle H2 = Player2->AddRequest(Shared.Get(), FACFCameraRequestParams());
	TestTrue(TEXT("Handles are unique across directors"), H1 != H2);
	TestFalse(TEXT("Player2 cannot release Player1's request"), Player2->RemoveRequest(H1));

	Driver1.Step(Player1.Get(), 0.75f);
	Driver2.Step(Player2.Get(), 0.25f);
	TestTrue(TEXT("Player1 weight"), FMath::IsNearlyEqual(Player1->GetLastResult().Weight, 0.75f, 1e-4f));
	TestTrue(TEXT("Player2 weight"), FMath::IsNearlyEqual(Player2->GetLastResult().Weight, 0.25f, 1e-4f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
