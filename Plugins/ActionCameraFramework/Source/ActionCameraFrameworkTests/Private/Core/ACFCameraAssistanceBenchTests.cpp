#include "Misc/AutomationTest.h"
#include "ACFCameraProfiler.h" // defines ACF_WITH_PROFILER

#if WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER

#include "ACFCameraTestUtils.h"
#include "Components/SceneComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7B-1 measurement (not a regression test): the cost of the Orbital Follow movement assistance in its states, on the
 * same standalone director evaluation (Orbital Follow only, so the assistance is a visible share). Runs only with
 * -ACFProfile (e.g. in the profile context of RunTests.sh, filter ActionCameraFramework.Profile) and writes
 * Saved/ACFProfile/7B1_Assistance.md:
 *   0 NoRecenter     recenter off (the evaluation without any recenter code)
 *   1 Disabled       Recenter When Idle (Phase 2 path), assistance off, target moving
 *   2 Waiting        assistance on, never starts (Recenter Delay 1e9), target moving
 *   3 Assisting      assistance on, target on a circle (the heading changes every frame: always turning)
 *   4 ManualInput    assistance on, camera input every frame (assistance suppressed by input)
 * CPU: 9 interleaved rounds x 4000 evaluations, profiler off. Allocations: one extra run of 600 evaluations with the
 * profiler and the allocation counter on. Pose: yaw / location after the timing run (deterministic inputs).
 */
namespace ACFAssistBench
{
	enum class EVariant : uint8 { NoRecenter, Disabled, Waiting, Assisting, ManualInput, Num };

	static const TCHAR* Name(EVariant V)
	{
		static const TCHAR* Names[] = { TEXT("NoRecenter"), TEXT("Disabled (Phase 2 recenter)"), TEXT("Waiting"), TEXT("Assisting"), TEXT("ManualInput") };
		return Names[static_cast<int32>(V)];
	}

	struct FResult
	{
		double Us = 0.0;
		FACFCameraPose Pose;
		EACFCameraAssistanceState State = EACFCameraAssistanceState::Disabled;
	};

	static FResult Run(EVariant Variant, int32 Evaluations, bool bProfile, uint32* OutAllocations = nullptr)
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<USceneComponent> Target(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		Target->SetMobility(EComponentMobility::Movable);
		TStrongObjectPtr<UACFCameraPreset> Preset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset.Get());
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		Orbit->RotationDamping = FVector2D(0.1f, 0.1f);
		Orbit->bRecenterWhenIdle = Variant == EVariant::Disabled;
		Orbit->RecenterDelay = Variant == EVariant::Waiting ? 1.0e9f : 0.3f;
		Orbit->bEnableMovementAssistance = Variant >= EVariant::Waiting;
		Orbit->RecenterHeading = EACFCameraRecenterHeading::MovementDirection;
		Preset->Nodes.Add(Orbit);
		FACFCameraRequestParams Params;
		Params.FollowTarget.Component = Target.Get();
		Director->AddRequest(Preset.Get(), Params);
		Director->SetProfilerForced(bProfile);
		FACFCameraEvaluationContext Context;
		Context.DeltaTime = 1.f / 60.f;
		Context.UpstreamPose.FieldOfView = 90.f;
		auto Frame = [&](int32 I)
		{
			const double Angle = I * (1.0 / 60.0) * FMath::DegreesToRadians(60.0); // a 60 deg/s circle of 4 m (~420 cm/s)
			Target->SetWorldLocation(FVector(FMath::Cos(Angle) * 400.0, FMath::Sin(Angle) * 400.0, 100.0));
			if (Variant == EVariant::ManualInput)
			{
				Director->SetCameraInputRate(FVector2D(10.0, 0.0));
			}
			Context.FrameNumber = 80000000ull + I;
			Director->Evaluate(Context);
		};
		for (int32 I = 0; I < 120; ++I) // warm up: activation, waits passed
		{
			Frame(I);
		}
		if (OutAllocations)
		{
			Director->GetProfiler().Clear();
			FACFCameraAllocationCounter::SetCounting(true);
		}
		const uint64 T0 = FPlatformTime::Cycles64();
		for (int32 I = 120; I < 120 + Evaluations; ++I)
		{
			Frame(I);
		}
		FResult Result;
		Result.Us = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0 / Evaluations;
		if (OutAllocations)
		{
			FACFCameraAllocationCounter::SetCounting(false);
			*OutAllocations = 0;
			const FACFCameraProfiler& Profiler = Director->GetProfiler();
			for (int32 I = 0; I < Profiler.Num(); ++I)
			{
				*OutAllocations += Profiler.GetFrame(I).Allocations;
			}
		}
		Result.Pose = Director->GetLastResult().Pose;
		Result.State = Director->GetLastDebug().AssistanceState;
		Director->SetProfilerForced(false);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistBenchTest, "ActionCameraFramework.Profile.AssistanceVariants", ACFTests::Flags)
bool FACFAssistBenchTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistBench;
	if (!FParse::Param(FCommandLine::Get(), TEXT("ACFProfile")))
	{
		AddInfo(TEXT("Skipped: Phase 7B-1 measurement, run with -ACFProfile."));
		return true;
	}
	constexpr int32 NumVariants = static_cast<int32>(EVariant::Num);
	TArray<double> Us[NumVariants];
	FResult Last[NumVariants];
	for (int32 Round = 0; Round < 9; ++Round) // interleaved: drift / turbo spread over every variant
	{
		for (int32 V = 0; V < NumVariants; ++V)
		{
			Last[V] = Run(static_cast<EVariant>(V), 4000, false);
			Us[V].Add(Last[V].Us);
		}
	}
	const bool bCounter = FACFCameraAllocationCounter::Install();
	FString Rows;
	const double Base = FACFCameraProfileStats::Compute(Us[0]).Median;
	for (int32 V = 0; V < NumVariants; ++V)
	{
		uint32 Allocations = 0;
		if (bCounter)
		{
			Run(static_cast<EVariant>(V), 600, true, &Allocations);
		}
		const FACFCameraProfileStats S = FACFCameraProfileStats::Compute(Us[V]); // sorts: Us[V][0] is the minimum
		Rows += FString::Printf(TEXT("| %s | %.3f | %.3f | %.3f | %+.3f | %s | %s | %.2f | %s |\n"), Name(static_cast<EVariant>(V)), S.Median, Us[V][0], S.Max, S.Median - Base,
			bCounter ? *FString::FromInt(Allocations) : TEXT("n/a"), *UEnum::GetDisplayValueAsText(Last[V].State).ToString(), Last[V].Pose.Rotation.Yaw, *Last[V].Pose.Location.ToCompactString());
		if (bCounter)
		{
			TestEqual(FString::Printf(TEXT("%s: no allocation in 600 evaluations"), Name(static_cast<EVariant>(V))), Allocations, 0u);
		}
	}
	const FString Text = FString::Printf(TEXT("# Phase 7B-1: movement assistance cost\n\nStandalone director, Orbital Follow only (rotation damping 0.1), target on a 4 m circle at 60 deg/s, 1/60 s steps. ")
		TEXT("CPU: 9 interleaved rounds x 4000 evaluations, us per evaluation (profiler off). Allocations: 600 evaluations with the allocation counter. Pose after the last timing run.\n\n")
		TEXT("| Variant | median us | min | max | vs NoRecenter | allocations | state | yaw | location |\n|---|---|---|---|---|---|---|---|---|\n%s"), *Rows);
	FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("ACFProfile") / TEXT("7B1_Assistance.md")));
	AddInfo(Text);
	TestTrue(TEXT("Assisting variant really assists"), Last[static_cast<int32>(EVariant::Assisting)].State == EACFCameraAssistanceState::Assisting);
	TestTrue(TEXT("Waiting variant waits"), Last[static_cast<int32>(EVariant::Waiting)].State == EACFCameraAssistanceState::Waiting);
	TestTrue(TEXT("Manual input variant is suppressed"), Last[static_cast<int32>(EVariant::ManualInput)].State == EACFCameraAssistanceState::Suppressed);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER
