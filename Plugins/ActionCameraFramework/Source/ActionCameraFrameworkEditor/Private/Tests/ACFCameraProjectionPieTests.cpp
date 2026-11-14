#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_Composers.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "SceneView.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"
#include "GameMapsSettings.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7A (stabilization): on the real, rendered PIE viewport (run with RunTests.sh --context editor-rendered), ACF's
 * composition must land where the renderer really projects: the Rotation Composer's screen position, the Lock-On focus,
 * Group Framing's framing size, for the full view, a portrait and a very wide player rectangle, right after a FOV change,
 * with an external view target and in split screen. Under -NullRHI (no viewport size) only the fallback is checked.
 */
namespace ACFProjectionPieTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FState
	{
		FAutomationTestBase* Test = nullptr;
		TStrongObjectPtr<UACFCameraPreset> Composer;
		TStrongObjectPtr<UACFCameraPreset> LockOn;
		TStrongObjectPtr<UACFCameraPreset> Group;
		FACFCameraRequestHandle Handle;
		FACFCameraRequestHandle Second;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TWeakObjectPtr<ULocalPlayer> Extra;
		uint64 Mark = 0;
		bool bRendered = false;
		ETwoPlayerSplitScreenType::Type SavedLayout = ETwoPlayerSplitScreenType::Horizontal;
	};
	using FStep = TFunction<bool(FState&)>;

	class FCommand : public IAutomationLatentCommand
	{
	public:
		FCommand(TSharedRef<FState> InState, TArray<FStep> InSteps) : State(InState), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			StepStart = StepStart == 0.0 ? Now : StepStart;
			if (Now - StepStart > 60.0)
			{
				State->Test->AddError(FString::Printf(TEXT("Projection PIE step %d timed out."), Index));
				return true;
			}
			if (Index < Steps.Num() && Steps[Index](*State))
			{
				++Index;
				StepStart = 0.0;
			}
			return Index >= Steps.Num();
		}
	private:
		TSharedRef<FState> State;
		TArray<FStep> Steps;
		int32 Index = 0;
		double StepStart = 0.0;
	};

	inline FStep Wait(int32 Frames)
	{
		return [Frames](FState& S) { if (S.Mark == 0) { S.Mark = GFrameCounter; } if (GFrameCounter < S.Mark + Frames) { return false; } S.Mark = 0; return true; };
	}

	inline UWorld* PieWorld() { return GEditor ? GEditor->PlayWorld.Get() : nullptr; }

	inline ULocalPlayer* Player(int32 Index = 0)
	{
		UWorld* World = PieWorld();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance && GameInstance->GetLocalPlayers().IsValidIndex(Index) ? GameInstance->GetLocalPlayers()[Index] : nullptr;
	}

	inline APlayerController* PC(ULocalPlayer* LocalPlayer) { return LocalPlayer ? LocalPlayer->GetPlayerController(PieWorld()) : nullptr; }
	inline UACFCameraSubsystem* Acf(ULocalPlayer* LocalPlayer) { return LocalPlayer ? LocalPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr; }

	/** The renderer's projection of this player's view (what was rendered last frame). */
	inline bool Rendered(ULocalPlayer* LocalPlayer, FSceneViewProjectionData& Out)
	{
		UGameViewportClient* Client = PieWorld() ? PieWorld()->GetGameViewport() : nullptr;
		FViewport* Viewport = Client ? Client->Viewport : nullptr;
		return LocalPlayer && Viewport && Viewport->GetSizeXY().X > 0 && LocalPlayer->GetProjectionData(Viewport, Out);
	}

	inline bool Ndc(const FMatrix& ViewProjection, const FVector& World, FVector2D& Out)
	{
		const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(World, 1.0));
		if (Clip.W <= UE_SMALL_NUMBER)
		{
			return false;
		}
		Out = FVector2D(Clip.X / Clip.W, Clip.Y / Clip.W);
		return true;
	}

	/** Fixed rotation and FOV (no location), Follow on the pawn, Rotation Composer on the pawn at (0.3, 0.2). */
	inline UACFCameraPreset* MakeComposer()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		UACFCameraNode_FixedPose* Lens = NewObject<UACFCameraNode_FixedPose>(Preset);
		Lens->bOverrideLocation = false;
		Lens->bOverrideRotation = false;
		Lens->FieldOfView = 90.f;
		UACFCameraNode_Follow* Follow = NewObject<UACFCameraNode_Follow>(Preset);
		Follow->Offset = FVector(-450.0, 0.0, 220.0);
		Follow->OffsetSpace = EACFCameraOffsetSpace::World;
		UACFCameraNode_RotationComposer* Composer = NewObject<UACFCameraNode_RotationComposer>(Preset);
		Composer->Composition.ScreenPosition = FVector2D(0.3, 0.2);
		Composer->Composition.DeadZoneSize = FVector2D::ZeroVector;
		Composer->Damping = FVector2D::ZeroVector;
		Preset->Nodes = { Lens, Follow, Composer };
		return Preset;
	}

	inline UACFCameraPreset* MakeLockOn()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		UACFCameraNode_LockOn* LockOn = NewObject<UACFCameraNode_LockOn>(Preset);
		LockOn->RotationDamping = FVector2D::ZeroVector;
		LockOn->LockTargetDampingTime = 0.f;
		LockOn->RangeDampingTime = 0.f;
		LockOn->PivotDamping = FACFCameraPositionDamping();
		LockOn->FocusScreenPosition = FVector2D(0.15, 0.1);
		Preset->Nodes = { LockOn };
		return Preset;
	}

	/** Fixed rotation (-10, 0, 0) and FOV 50, Follow on the pawn, Group Framing in FOV mode filling 0.8 x 0.7. */
	inline UACFCameraPreset* MakeGroup()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		UACFCameraNode_FixedPose* Lens = NewObject<UACFCameraNode_FixedPose>(Preset);
		Lens->bOverrideLocation = false;
		Lens->Rotation = FRotator(-10.0, 0.0, 0.0);
		Lens->FieldOfView = 50.f;
		UACFCameraNode_Follow* Follow = NewObject<UACFCameraNode_Follow>(Preset);
		Follow->Offset = FVector(-450.0, 0.0, 220.0);
		Follow->OffsetSpace = EACFCameraOffsetSpace::World;
		UACFCameraNode_GroupFraming* Group = NewObject<UACFCameraNode_GroupFraming>(Preset);
		Group->Mode = EACFCameraGroupFramingMode::FieldOfView;
		Group->Policy = EACFCameraGroupFramingPolicy::Fill;
		Group->FramingSize = FVector2D(0.8, 0.7);
		Group->MinFieldOfView = 20.f;
		Group->MaxFieldOfView = 150.f;
		Group->WidenDampingTime = 0.f;
		Group->NarrowDampingTime = 0.f;
		Preset->Nodes = { Lens, Follow, Group };
		return Preset;
	}

	/** The composer target (the pawn) lands at 2 x (0.3, 0.2) in the rendered view. */
	inline void CheckComposer(FState& S, const TCHAR* Case, ULocalPlayer* LocalPlayer = nullptr)
	{
		LocalPlayer = LocalPlayer ? LocalPlayer : Player();
		FSceneViewProjectionData Projection;
		if (!Rendered(LocalPlayer, Projection))
		{
			return;
		}
		const FACFCameraEvaluationResult& R = Acf(LocalPlayer)->GetDirector()->GetLastResult();
		FVector2D Pawn;
		Ndc(Projection.ComputeViewProjectionMatrix(), PC(LocalPlayer)->GetPawn()->GetActorLocation(), Pawn);
		const FIntRect Rect = Projection.GetConstrainedViewRect();
		const double RectAspect = static_cast<double>(Rect.Width()) / Rect.Height();
		const double RenderedScale = (1.0 / Projection.ProjectionMatrix.M[0][0]) / FMath::Tan(FMath::DegreesToRadians(R.Pose.FieldOfView) * 0.5);
		S.Test->TestTrue(FString::Printf(TEXT("%s: composer target rendered at (%.4f, %.4f) = (0.6, 0.4)"), Case, Pawn.X, Pawn.Y), Pawn.Equals(FVector2D(0.6, 0.4), 1e-3));
		S.Test->TestTrue(FString::Printf(TEXT("%s: ACF aspect %.4f = rendered view %d x %d (%.4f)"), Case, R.AspectRatio, Rect.Width(), Rect.Height(), RectAspect),
			FMath::IsNearlyEqual(R.AspectRatio, RectAspect, 1e-3));
		S.Test->TestTrue(FString::Printf(TEXT("%s: ACF FOV scale %.5f = rendered %.5f"), Case, R.HorizontalFovScale, RenderedScale), FMath::IsNearlyEqual(R.HorizontalFovScale, RenderedScale, 2e-4));
		S.Test->AddInfo(FString::Printf(TEXT("%s: view %d x %d, pose FOV %.2f renders %.3f"), Case, Rect.Width(), Rect.Height(), R.Pose.FieldOfView,
			FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Projection.ProjectionMatrix.M[0][0]))));
	}

	/** Re-measures the composer with the pre-7A lens (ACF.Projection.RenderedLens 0): how far off it was on this view. */
	inline void AddClassicComparison(TArray<FStep>& Steps, const TCHAR* Case, bool bExtraPlayer)
	{
		Steps.Add([](FState& S) { IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Projection.RenderedLens"))->Set(0, ECVF_SetByCode); return true; });
		Steps.Add(Wait(6));
		Steps.Add([Case, bExtraPlayer](FState& S)
		{
			ULocalPlayer* LocalPlayer = bExtraPlayer ? S.Extra.Get() : Player();
			FSceneViewProjectionData Projection;
			if (S.bRendered && LocalPlayer && Rendered(LocalPlayer, Projection))
			{
				FVector2D Pawn;
				Ndc(Projection.ComputeViewProjectionMatrix(), PC(LocalPlayer)->GetPawn()->GetActorLocation(), Pawn);
				const FIntRect Rect = Projection.GetConstrainedViewRect();
				S.Test->AddInfo(FString::Printf(TEXT("%s, pre-7A lens: the composer target was rendered at (%.4f, %.4f) instead of (0.6, 0.4): %.1f / %.1f px off on %d x %d"),
					Case, Pawn.X, Pawn.Y, (Pawn.X - 0.6) * 0.5 * Rect.Width(), (Pawn.Y - 0.4) * 0.5 * Rect.Height(), Rect.Width(), Rect.Height()));
				S.Test->TestFalse(FString::Printf(TEXT("%s: the pre-7A lens is measurably off here (what is fixed)"), Case), Pawn.Equals(FVector2D(0.6, 0.4), 2e-3));
			}
			IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Projection.RenderedLens"))->Set(1, ECVF_SetByCode);
			return true;
		});
		Steps.Add(Wait(6));
	}
	inline AActor* SpawnPoint(FState& S, const FVector& Location)
	{
		AActor* Actor = PieWorld()->SpawnActor<ACameraActor>(Location, FRotator::ZeroRotator); // any actor with a root component
		S.Spawned.Add(Actor);
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProjectionPieTest, "ActionCameraFramework.Editor.Projection.RenderedPieComposition", ACFProjectionPieTests::Flags)
bool FACFProjectionPieTest::RunTest(const FString& Parameters)
{
	using namespace ACFProjectionPieTests;
	TSharedRef<FState> State = MakeShared<FState>();
	State->Test = this;
	State->Composer.Reset(MakeComposer());
	State->LockOn.Reset(MakeLockOn());
	State->Group.Reset(MakeGroup());
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/ActionCameraFramework/Samples/L_ACFCameraTest")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	TArray<FStep> Steps;
	Steps.Add([](FState& S)
	{
		ULocalPlayer* LocalPlayer = Player();
		APlayerController* Controller = PC(LocalPlayer);
		if (!Controller || !Controller->GetPawn() || !Controller->PlayerCameraManager || !Acf(LocalPlayer))
		{
			return false;
		}
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = Controller->GetPawn();
		Params.LookAtTarget.Actor = Controller->GetPawn();
		S.Handle = Acf(LocalPlayer)->RequestCamera(S.Composer.Get(), Params);
		return true;
	});
	Steps.Add(Wait(10));
	Steps.Add([](FState& S)
	{
		FSceneViewProjectionData Projection;
		S.bRendered = Rendered(Player(), Projection);
		const FACFCameraEvaluationResult& R = Acf(Player())->GetDirector()->GetLastResult();
		if (!S.bRendered)
		{
			// 12. NullRHI: no viewport size, the classic lens is kept (nothing changes there).
			S.Test->TestEqual(TEXT("12. No viewport: the classic FOV (scale 1)"), R.HorizontalFovScale, 1.f);
			S.Test->AddInfo(TEXT("The PIE viewport has no size (-NullRHI): the rendered comparisons need --context editor-rendered."));
			return true;
		}
		CheckComposer(S, TEXT("2. Full view"));
		return true;
	});
	AddClassicComparison(Steps, TEXT("2. Full view 1503 x 837"), false);
	// 11. A FOV change through runtime tuning: the composition is right from the first frame with the new FOV.
	Steps.Add([](FState& S)
	{
		UACFCameraDirector* Director = Acf(Player())->GetDirector();
		S.Test->TestEqual(TEXT("11. Start tuning"), Director->StartTuning(S.Handle), EACFCameraTuningResult::Ok);
		S.Test->TestEqual(TEXT("11. FOV 62"), Director->SetTuningValue(S.Handle, EACFCameraTuningParameter::FieldOfView, FVector(62.0, 0.0, 0.0), 0), EACFCameraTuningResult::Ok);
		S.Mark = GFrameCounter;
		return true;
	});
	Steps.Add([](FState& S)
	{
		const FACFCameraEvaluationResult& R = Acf(Player())->GetDirector()->GetLastResult();
		if (R.FrameNumber <= S.Mark)
		{
			return false;
		}
		// The same frame's pose / FOV / lens: project the pawn with the engine's matrices for this result.
		ULocalPlayer* LocalPlayer = Player();
		UGameViewportClient* Client = PieWorld()->GetGameViewport();
		if (S.bRendered)
		{
			FMinimalViewInfo View = PC(LocalPlayer)->PlayerCameraManager->GetCameraCacheView();
			View.FOV = R.Pose.FieldOfView;
			View.Location = R.Pose.Location;
			View.Rotation = R.Pose.Rotation;
			FSceneViewProjectionData Projection;
			const FIntPoint Size = Client->Viewport->GetSizeXY();
			Projection.SetViewRectangle(FIntRect(0, 0, Size.X, Size.Y));
			Projection.ViewOrigin = View.Location;
			Projection.ViewRotationMatrix = FInverseRotationMatrix(View.Rotation) * FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
			FMinimalViewInfo::CalculateProjectionMatrixGivenView(View, LocalPlayer->AspectRatioAxisConstraint, Client->Viewport, Projection);
			FVector2D Pawn;
			Ndc(Projection.ComputeViewProjectionMatrix(), PC(LocalPlayer)->GetPawn()->GetActorLocation(), Pawn);
			S.Test->TestTrue(FString::Printf(TEXT("11/13. First frame at FOV %.1f: the composition matches this frame's projection (%.4f, %.4f)"), R.Pose.FieldOfView, Pawn.X, Pawn.Y),
				FMath::IsNearlyEqual(R.Pose.FieldOfView, 62.f, 0.01f) && Pawn.Equals(FVector2D(0.6, 0.4), 1e-3));
		}
		return true;
	});
	Steps.Add(Wait(4));
	Steps.Add([](FState& S)
	{
		if (S.bRendered)
		{
			CheckComposer(S, TEXT("11. Tuned FOV 62"));
		}
		Acf(Player())->GetDirector()->StopTuning(S.Handle);
		return true;
	});
	// 8. Lock-On: the focus point at the focus screen position, player and target on screen.
	Steps.Add([](FState& S)
	{
		APawn* Pawn = PC(Player())->GetPawn();
		AActor* Target = SpawnPoint(S, Pawn->GetActorLocation() + FVector(700.0, 250.0, 60.0));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = Pawn;
		Params.LookAtTarget.Actor = Target;
		Params.Priority = 10;
		S.Second = Acf(Player())->RequestCamera(S.LockOn.Get(), Params);
		return true;
	});
	Steps.Add(Wait(30));
	Steps.Add([](FState& S)
	{
		FSceneViewProjectionData Projection;
		if (S.bRendered && Rendered(Player(), Projection))
		{
			const FMatrix ViewProjection = Projection.ComputeViewProjectionMatrix();
			const FACFCameraRigDebug& Debug = Acf(Player())->GetDirector()->GetLastDebug();
			FVector2D Focus, PawnNdc, TargetNdc;
			Ndc(ViewProjection, Debug.LockOnFocus, Focus);
			Ndc(ViewProjection, PC(Player())->GetPawn()->GetActorLocation(), PawnNdc);
			Ndc(ViewProjection, S.Spawned.Last()->GetActorLocation(), TargetNdc);
			S.Test->TestTrue(FString::Printf(TEXT("8. Lock-On focus rendered at (%.4f, %.4f) = 2 x (0.15, 0.1)"), Focus.X, Focus.Y), Focus.Equals(FVector2D(0.3, 0.2), 2e-3));
			S.Test->TestTrue(FString::Printf(TEXT("8. Player (%.3f, %.3f) and target (%.3f, %.3f) on screen"), PawnNdc.X, PawnNdc.Y, TargetNdc.X, TargetNdc.Y),
				FMath::Abs(PawnNdc.X) < 1.0 && FMath::Abs(PawnNdc.Y) < 1.0 && FMath::Abs(TargetNdc.X) < 1.0 && FMath::Abs(TargetNdc.Y) < 1.0);
		}
		Acf(Player())->ReleaseCamera(S.Second);
		return true;
	});
	// 9. Group Framing (FOV mode, Fill): the limiting subject fills exactly the framing size of the rendered view.
	Steps.Add([](FState& S)
	{
		APawn* Pawn = PC(Player())->GetPawn();
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = Pawn;
		Params.Priority = 10;
		S.Second = Acf(Player())->RequestCamera(S.Group.Get(), Params);
		for (const FVector Offset : { FVector(600.0, -420.0, 0.0), FVector(900.0, 380.0, 150.0), FVector(500.0, 60.0, -120.0) })
		{
			FACFCameraTargetGroupMember Member;
			Member.Target.Actor = SpawnPoint(S, Pawn->GetActorLocation() + Offset);
			Member.Radius = 40.f;
			Acf(Player())->AddCameraGroupMember(S.Second, Member);
		}
		return true;
	});
	Steps.Add(Wait(20));
	Steps.Add([](FState& S)
	{
		FSceneViewProjectionData Projection;
		if (S.bRendered && Rendered(Player(), Projection))
		{
			const FACFCameraRigDebug& Debug = Acf(Player())->GetDirector()->GetLastDebug();
			const APlayerCameraManager* PCM = PC(Player())->PlayerCameraManager;
			const double TanH = 1.0 / Projection.ProjectionMatrix.M[0][0];
			const double TanV = 1.0 / Projection.ProjectionMatrix.M[1][1];
			double Fill = 0.0;
			for (int32 Index = 0; Index < Debug.NumGroupSubjects; ++Index)
			{
				const FVector L = PCM->GetCameraRotation().UnrotateVector(FVector(Debug.GroupSubjects[Index]) - PCM->GetCameraLocation());
				const double R = Debug.GroupSubjects[Index].W;
				const double AngH = FMath::Atan2(FMath::Abs(L.Y), L.X) + FMath::Asin(R / FMath::Sqrt(L.X * L.X + L.Y * L.Y));
				const double AngV = FMath::Atan2(FMath::Abs(L.Z), L.X) + FMath::Asin(R / FMath::Sqrt(L.X * L.X + L.Z * L.Z));
				Fill = FMath::Max(Fill, FMath::Max(FMath::Tan(AngH) / (TanH * 0.8), FMath::Tan(AngV) / (TanV * 0.7)));
			}
			S.Test->TestTrue(FString::Printf(TEXT("9. Group Framing: %d subjects, the limiting one fills the rendered framing size exactly (%.5f)"), Debug.NumGroupSubjects, Fill),
				Debug.NumGroupSubjects == 3 && FMath::IsNearlyEqual(Fill, 1.0, 2e-3));
		}
		Acf(Player())->ReleaseCamera(S.Second);
		return true;
	});
	// 10. An external view target on screen (a Sequencer camera cut): its own camera settings, ACF bypassed, nothing breaks.
	Steps.Add([](FState& S)
	{
		APlayerController* Controller = PC(Player());
		AActor* Camera = SpawnPoint(S, Controller->PlayerCameraManager->GetCameraLocation() + FVector(0.0, 0.0, 300.0));
		Acf(Player())->AddExternalViewTarget(Camera, Camera);
		Controller->SetViewTarget(Camera);
		return true;
	});
	Steps.Add(Wait(5));
	Steps.Add([](FState& S)
	{
		const FACFCameraEvaluationResult& R = Acf(Player())->GetDirector()->GetLastResult();
		S.Test->TestTrue(TEXT("10. External view target: ACF bypassed"), R.bOutputBypassed);
		S.Test->TestTrue(FString::Printf(TEXT("10. Projection inputs stay valid (aspect %.4f, scale %.5f)"), R.AspectRatio, R.HorizontalFovScale),
			FMath::IsFinite(R.AspectRatio) && R.AspectRatio > 0.f && FMath::IsFinite(R.HorizontalFovScale) && R.HorizontalFovScale > 0.f);
		AActor* Camera = S.Spawned.Last().Get();
		Acf(Player())->RemoveExternalViewTarget(Camera, Camera);
		PC(Player())->SetViewTarget(PC(Player())->GetPawn());
		return true;
	});
	Steps.Add(Wait(5));
	Steps.Add([](FState& S)
	{
		if (S.bRendered)
		{
			CheckComposer(S, TEXT("10. Back from the external view target"));
		}
		// 6 / 3 / 4 / 7 / 13: split screen. A second local player: the engine lays the players out (top / bottom: two very wide
		// views of 3.6 : 1; left / right: two portrait views). Each player's lens follows its own rectangle.
		FString Error;
		S.Extra = PieWorld()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(5), Error, true);
		S.Mark = GFrameCounter;
		return true;
	});
	Steps.Add([](FState& S)
	{
		// 13. The first evaluation after the layout change: the new rectangle of this frame, not the last rendered one.
		const FACFCameraEvaluationResult& R = Acf(Player())->GetDirector()->GetLastResult();
		if (R.FrameNumber <= S.Mark)
		{
			return false;
		}
		if (S.bRendered)
		{
			FSceneViewProjectionData Projection;
			Rendered(Player(), Projection);
			const FIntRect Rect = Projection.GetConstrainedViewRect();
			S.Test->TestTrue(FString::Printf(TEXT("13. First evaluation after the split (frame %llu): aspect %.4f = the new view %d x %d"), R.FrameNumber, R.AspectRatio, Rect.Width(), Rect.Height()),
				FMath::IsNearlyEqual(R.AspectRatio, static_cast<double>(Rect.Width()) / Rect.Height(), 1e-3));
		}
		S.Mark = 0;
		return true;
	});
	Steps.Add([](FState& S)
	{
		ULocalPlayer* Extra = S.Extra.Get();
		APlayerController* Controller = PC(Extra);
		if (!Controller || !Controller->GetPawn() || !Acf(Extra))
		{
			return false;
		}
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = Controller->GetPawn();
		Params.LookAtTarget.Actor = Controller->GetPawn();
		Acf(Extra)->RequestCamera(S.Composer.Get(), Params);
		return true;
	});
	for (const bool bVertical : { false, true })
	{
		Steps.Add([bVertical](FState& S)
		{
			UGameMapsSettings* Maps = GetMutableDefault<UGameMapsSettings>();
			S.SavedLayout = Maps->TwoPlayerSplitscreenLayout;
			Maps->TwoPlayerSplitscreenLayout = bVertical ? ETwoPlayerSplitScreenType::Vertical : ETwoPlayerSplitScreenType::Horizontal;
			return true;
		});
		Steps.Add(Wait(12));
		AddClassicComparison(Steps, bVertical ? TEXT("4. Split screen left / right (portrait), player 1") : TEXT("3. Split screen top / bottom (very wide), player 1"), false);
		Steps.Add([bVertical](FState& S)
		{
			if (S.bRendered)
			{
				for (ULocalPlayer* LocalPlayer : { Player(0), S.Extra.Get() })
				{
					CheckComposer(S, *FString::Printf(TEXT("6. Split screen %s, %s"), bVertical ? TEXT("left / right (4. portrait)") : TEXT("top / bottom (3. very wide)"),
						*GetNameSafe(LocalPlayer)), LocalPlayer);
				}
			}
			GetMutableDefault<UGameMapsSettings>()->TwoPlayerSplitscreenLayout = S.SavedLayout;
			return true;
		});
	}
	Steps.Add([](FState& S)
	{
		if (ULocalPlayer* Extra = S.Extra.Get())
		{
			if (UACFCameraSubsystem* ExtraAcf = Acf(Extra))
			{
				ExtraAcf->ReleaseAllCameras(true);
			}
			APawn* ExtraPawn = PC(Extra) ? PC(Extra)->GetPawn() : nullptr;
			PieWorld()->GetGameInstance()->RemoveLocalPlayer(Extra);
			if (ExtraPawn)
			{
				ExtraPawn->Destroy(); // RemoveLocalPlayer leaves the pawn behind (7A-2)
			}
		}
		for (const TWeakObjectPtr<AActor>& Actor : S.Spawned)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}
		Acf(Player())->ReleaseAllCameras(true);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(State, MoveTemp(Steps)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
