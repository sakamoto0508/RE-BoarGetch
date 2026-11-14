#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "Camera/CameraTypes.h"
#include "HAL/IConsoleManager.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_Composers.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraTargetGroup.h"
#include "SceneView.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7A (stabilization): ACF's screen-space math (composers, Lock-On, Group Framing, framing recovery) against the
 * projection the renderer really uses. UE 5.8 projects with AspectRatio_MaintainYFOV by default: a view wider than the
 * camera's aspect renders a wider horizontal FOV than the pose's (1503 x 837 at FOV 90 renders 90.575 degrees). README 27 章.
 */
namespace ACFProjectionTests
{
	struct FCase
	{
		FIntPoint Size;
		EAspectRatioAxisConstraint Constraint;
		const TCHAR* Name;
	};

	const FCase Cases[] = {
		{ FIntPoint(1920, 1080), AspectRatio_MaintainYFOV, TEXT("16:9, MaintainYFOV") },
		{ FIntPoint(1280, 720), AspectRatio_MaintainXFOV, TEXT("16:9, MaintainXFOV") },
		{ FIntPoint(1503, 837), AspectRatio_MaintainYFOV, TEXT("1503x837, MaintainYFOV") },
		{ FIntPoint(1503, 837), AspectRatio_MaintainXFOV, TEXT("1503x837, MaintainXFOV") },
		{ FIntPoint(1503, 837), AspectRatio_MajorAxisFOV, TEXT("1503x837, MajorAxisFOV") },
		{ FIntPoint(2560, 1080), AspectRatio_MaintainYFOV, TEXT("21:9, MaintainYFOV") },
		{ FIntPoint(2560, 1080), AspectRatio_MajorAxisFOV, TEXT("21:9, MajorAxisFOV") },
		{ FIntPoint(1080, 1920), AspectRatio_MaintainYFOV, TEXT("portrait, MaintainYFOV") },
		{ FIntPoint(1080, 1920), AspectRatio_MajorAxisFOV, TEXT("portrait, MajorAxisFOV") },
		{ FIntPoint(1080, 1920), AspectRatio_MaintainXFOV, TEXT("portrait, MaintainXFOV") },
		{ FIntPoint(960, 1080), AspectRatio_MaintainYFOV, TEXT("split screen half, MaintainYFOV") },
	};

	/** The engine's world -> NDC for a camera at Location / Rotation with the pose FOV (what the renderer builds). */
	inline FMatrix EngineViewProjection(const FVector& Location, const FRotator& Rotation, float Fov, float CameraAspect, bool bConstrain,
		const FIntPoint& Size, EAspectRatioAxisConstraint Constraint)
	{
		FMinimalViewInfo View;
		View.FOV = Fov;
		View.AspectRatio = CameraAspect;
		View.bConstrainAspectRatio = bConstrain;
		FSceneViewProjectionData Projection;
		Projection.SetViewRectangle(FIntRect(0, 0, Size.X, Size.Y));
		FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, Constraint, FIntRect(0, 0, Size.X, Size.Y), Projection);
		const FMatrix ViewRotation = FInverseRotationMatrix(Rotation) * FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
		return FTranslationMatrix(-Location) * ViewRotation * Projection.ProjectionMatrix;
	}

	inline bool EngineNdc(const FMatrix& ViewProjection, const FVector& World, FVector2D& OutNdc)
	{
		const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(World, 1.0));
		if (Clip.W <= UE_SMALL_NUMBER)
		{
			return false;
		}
		OutNdc = FVector2D(Clip.X / Clip.W, Clip.Y / Clip.W);
		return true;
	}

	inline bool RenderedLens(const FIntPoint& Size, EAspectRatioAxisConstraint Constraint, float CameraAspect, bool bConstrain, float& OutScale, float& OutAspect)
	{
		FMinimalViewInfo Settings;
		Settings.AspectRatio = CameraAspect;
		Settings.bConstrainAspectRatio = bConstrain;
		return ACFFraming::ComputeRenderedLens(Settings, Constraint, FIntRect(0, 0, Size.X, Size.Y), nullptr, OutScale, OutAspect);
	}
}

// 1-6 (pure math): the lens ACF derives from the engine's projection code reproduces the engine's projection of any point,
// for every viewport shape and axis constraint, at pose FOVs other than the 90 degree reference.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProjectionLensTest, "ActionCameraFramework.Projection.RenderedLensMatchesTheEngine", ACFTests::Flags)
bool FACFProjectionLensTest::RunTest(const FString& Parameters)
{
	using namespace ACFProjectionTests;
	const FVector Location(120.0, -40.0, 210.0);
	const FRotator Rotation(-14.0, 28.0, 0.0);
	const FVector Points[] = { Location + Rotation.RotateVector(FVector(500.0, 180.0, -90.0)), Location + Rotation.RotateVector(FVector(900.0, -420.0, 260.0)),
		Location + Rotation.RotateVector(FVector(300.0, 10.0, 140.0)), Location + Rotation.RotateVector(FVector(650.0, 600.0, -300.0)) };
	IConsoleVariable* Legacy = IConsoleManager::Get().FindConsoleVariable(TEXT("r.UseLegacyMaintainYFOV"));
	const int32 LegacyBefore = Legacy ? Legacy->GetInt() : 0;
	for (int32 LegacyMode = 0; LegacyMode < (Legacy ? 2 : 1); ++LegacyMode)
	{
		if (Legacy)
		{
			Legacy->Set(LegacyMode, ECVF_SetByCode);
		}
		for (const FCase& Case : Cases)
		{
			for (const bool bConstrain : { false, true })
			{
				float Scale = 0.f, Aspect = 0.f;
				const FString Name = FString::Printf(TEXT("%s%s%s"), Case.Name, bConstrain ? TEXT(", constrained 16:9") : TEXT(""), LegacyMode ? TEXT(", legacy MaintainYFOV") : TEXT(""));
				if (!TestTrue(Name + TEXT(": lens computed"), RenderedLens(Case.Size, Case.Constraint, 16.f / 9.f, bConstrain, Scale, Aspect)))
				{
					continue;
				}
				for (const float Fov : { 55.f, 90.f, 118.f })
				{
					const FMatrix Engine = EngineViewProjection(Location, Rotation, Fov, 16.f / 9.f, bConstrain, Case.Size, Case.Constraint);
					const ACFFraming::FLens Lens = ACFFraming::MakeLens(Fov, Aspect, Scale);
					for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
					{
						FVector2D Ndc, Screen;
						double Depth = 0.0;
						if (EngineNdc(Engine, Points[Index], Ndc) && ACFFraming::Project(Location, Rotation, Lens, Points[Index], Screen, Depth))
						{
							TestTrue(FString::Printf(TEXT("%s, FOV %.0f, point %d: ACF (%.5f, %.5f) = engine (%.5f, %.5f)"), *Name, Fov, Index, Screen.X * 2.0, Screen.Y * 2.0, Ndc.X, Ndc.Y),
								Ndc.Equals(Screen * 2.0, 1e-4));
						}
					}
				}
			}
		}
	}
	if (Legacy)
	{
		Legacy->Set(LegacyBefore, ECVF_SetByCode);
	}

	// 1. 16:9 is the classic lens (the subsystem then keeps exactly the historical values); 2. the reported case.
	float Scale = 0.f, Aspect = 0.f;
	RenderedLens(FIntPoint(1920, 1080), AspectRatio_MaintainYFOV, 16.f / 9.f, false, Scale, Aspect);
	TestTrue(FString::Printf(TEXT("1. 16:9 MaintainYFOV: scale %.7f = 1"), Scale), FMath::IsNearlyEqual(Scale, 1.f, 1e-5f));
	RenderedLens(FIntPoint(1503, 837), AspectRatio_MaintainYFOV, 16.f / 9.f, false, Scale, Aspect);
	const float Rendered = FMath::RadiansToDegrees(2.f * FMath::Atan(Scale)); // pose FOV 90: tan(45) = 1
	TestTrue(FString::Printf(TEXT("2. 1503x837 MaintainYFOV at FOV 90 renders %.3f degrees (reported 90.575)"), Rendered), FMath::IsNearlyEqual(Rendered, 90.575f, 0.002f));
	TestTrue(FString::Printf(TEXT("2. 1503x837 projects with the view aspect %.4f"), Aspect), FMath::IsNearlyEqual(Aspect, 1503.f / 837.f, 1e-4f));
	RenderedLens(FIntPoint(1503, 837), AspectRatio_MaintainXFOV, 16.f / 9.f, false, Scale, Aspect);
	TestTrue(TEXT("MaintainXFOV: the pose FOV is the rendered one"), FMath::IsNearlyEqual(Scale, 1.f, 1e-5f));
	return true;
}

// 2, 8, 9 (director level): with the rendered lens of a 1503 x 837 MaintainYFOV view, the Rotation Composer and Group
// Framing frame exactly what the engine renders; with the classic lens (scale 1) they are off by about 1 %.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProjectionCompositionTest, "ActionCameraFramework.Projection.ComposerAndGroupFramingOnAWideView", ACFTests::Flags)
bool FACFProjectionCompositionTest::RunTest(const FString& Parameters)
{
	using namespace ACFProjectionTests;
	const FIntPoint Size(1503, 837);
	float Scale = 1.f, Aspect = 16.f / 9.f;
	RenderedLens(Size, AspectRatio_MaintainYFOV, 16.f / 9.f, false, Scale, Aspect);

	auto RunComposer = [&](float UseScale, FVector2D& OutRenderedNdc)
	{
		TStrongObjectPtr<UACFCameraPreset> Preset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
		UACFCameraNode_FixedPose* Place = NewObject<UACFCameraNode_FixedPose>(Preset.Get());
		Place->Location = FVector(-400.0, 0.0, 200.0);
		Place->bOverrideRotation = false;
		Place->FieldOfView = 80.f;
		UACFCameraNode_RotationComposer* Composer = NewObject<UACFCameraNode_RotationComposer>(Preset.Get());
		Composer->Composition.ScreenPosition = FVector2D(0.3, 0.2);
		Composer->Composition.DeadZoneSize = FVector2D::ZeroVector;
		Composer->Damping = FVector2D::ZeroVector;
		Preset->Nodes = { Place, Composer };
		TStrongObjectPtr<USceneComponent> Target(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		Target->SetWorldLocation(FVector(300.0, 120.0, 60.0));
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		FACFCameraRequestParams Params;
		Params.LookAtTarget.Component = Target.Get();
		Director->AddRequest(Preset.Get(), Params);
		FACFCameraPose Pose;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = 1.f / 60.f;
			Context.FrameNumber = 7700000 + Frame;
			Context.AspectRatio = Aspect;
			Context.HorizontalFovScale = UseScale;
			Pose = Director->Evaluate(Context).Pose;
		}
		const FMatrix Engine = EngineViewProjection(Pose.Location, Pose.Rotation, Pose.FieldOfView, 16.f / 9.f, false, Size, AspectRatio_MaintainYFOV);
		EngineNdc(Engine, Target->GetComponentLocation(), OutRenderedNdc);
	};
	FVector2D Fixed, Classic;
	RunComposer(Scale, Fixed);
	RunComposer(1.f, Classic);
	TestTrue(FString::Printf(TEXT("8. Composer with the rendered lens: target rendered at (%.4f, %.4f) = 2 x (0.3, 0.2)"), Fixed.X, Fixed.Y), Fixed.Equals(FVector2D(0.6, 0.4), 5e-4));
	AddInfo(FString::Printf(TEXT("Classic lens (scale 1) on this view: target rendered at (%.4f, %.4f), %.1f / %.1f px off on a 1503 x 837 view"),
		Classic.X, Classic.Y, (Classic.X - 0.6) * 0.5 * Size.X, (Classic.Y - 0.4) * 0.5 * Size.Y));
	TestFalse(TEXT("The classic lens is measurably off on this view (what was fixed)"), Classic.Equals(FVector2D(0.6, 0.4), 2e-3));

	// 9. Group Framing (FOV mode, Fill): the subject that limits the framing touches the framing edge in the rendered view.
	FACFCameraFramingSubjects Subjects;
	{ FACFCameraFramingSubject& S = Subjects.AddDefaulted_GetRef(); S.Location = FVector(600.0, -350.0, 40.0); S.Radius = 30.f; S.Weight = 1.f; }
	{ FACFCameraFramingSubject& S = Subjects.AddDefaulted_GetRef(); S.Location = FVector(500.0, 280.0, 120.0); S.Radius = 20.f; S.Weight = 1.f; }
	{ FACFCameraFramingSubject& S = Subjects.AddDefaulted_GetRef(); S.Location = FVector(800.0, 0.0, -150.0); S.Radius = 25.f; S.Weight = 1.f; }
	const FVector2D FramingSize(0.8, 0.7);
	const double PoseFov = UACFCameraNode_GroupFraming::ComputeRequiredFov(Subjects, FVector::ZeroVector, FRotator::ZeroRotator, Aspect, FramingSize, 0.f, 170.f, Scale);
	FMinimalViewInfo View;
	View.FOV = static_cast<float>(PoseFov);
	View.AspectRatio = 16.f / 9.f;
	FSceneViewProjectionData Projection;
	Projection.SetViewRectangle(FIntRect(0, 0, Size.X, Size.Y));
	FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, AspectRatio_MaintainYFOV, FIntRect(0, 0, Size.X, Size.Y), Projection);
	// The rendered half tangents of this projection, and the half tangents each sphere needs.
	const double TanH = 1.0 / Projection.ProjectionMatrix.M[0][0], TanV = 1.0 / Projection.ProjectionMatrix.M[1][1];
	double Fill = 0.0;
	for (const FACFCameraFramingSubject& Subject : Subjects)
	{
		const FVector& L = Subject.Location; // the cone tangent to the sphere's cross-section in each camera plane
		const double AngH = FMath::Atan2(FMath::Abs(L.Y), L.X) + FMath::Asin(Subject.Radius / FMath::Sqrt(L.X * L.X + L.Y * L.Y));
		const double AngV = FMath::Atan2(FMath::Abs(L.Z), L.X) + FMath::Asin(Subject.Radius / FMath::Sqrt(L.X * L.X + L.Z * L.Z));
		Fill = FMath::Max(Fill, FMath::Max(FMath::Tan(AngH) / (TanH * FramingSize.X), FMath::Tan(AngV) / (TanV * FramingSize.Y)));
	}
	TestTrue(FString::Printf(TEXT("9. Group Framing: the limiting subject fills exactly the framing size in the rendered view (%.5f)"), Fill), FMath::IsNearlyEqual(Fill, 1.0, 1e-4));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
