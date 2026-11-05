#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "UObject/Package.h"

/** Shared helpers for ACF automation tests (header-only so unity builds never see duplicates). */
namespace ACFTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	inline UACFCameraPreset* MakeFixedPreset(const FVector& Location, float FOV, const FACFCameraBlendSettings& Blend = FACFCameraBlendSettings::MakeCut())
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_FixedPose* Node = NewObject<UACFCameraNode_FixedPose>(Preset);
		Node->Location = Location;
		Node->Rotation = FRotator(-10.f, 45.f, 0.f);
		Node->FieldOfView = FOV;
		Preset->Nodes.Add(Node);
		Preset->DefaultBlendIn = Blend;
		Preset->DefaultBlendOut = Blend;
		return Preset;
	}

	inline FACFCameraBlendSettings Linear(float Seconds)
	{
		FACFCameraBlendSettings Blend;
		Blend.Curve = EACFCameraBlendCurve::Linear;
		Blend.Duration = Seconds;
		return Blend;
	}

	/** Simulates the output adapter: one evaluation per frame with a fixed upstream camera. */
	struct FFrameDriver
	{
		uint64 Frame = 1000;
		FACFCameraPose Upstream;

		const FACFCameraEvaluationResult& Step(UACFCameraDirector* Director, float DeltaTime)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = DeltaTime;
			Context.FrameNumber = ++Frame;
			Context.UpstreamPose = Upstream;
			return Director->Evaluate(Context);
		}

		const FACFCameraEvaluationResult& Repeat(UACFCameraDirector* Director, float DeltaTime)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = DeltaTime;
			Context.FrameNumber = Frame;
			Context.UpstreamPose = Upstream;
			return Director->Evaluate(Context);
		}
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
