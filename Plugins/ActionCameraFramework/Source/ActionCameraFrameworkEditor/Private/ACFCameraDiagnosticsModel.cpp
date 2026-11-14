#include "ACFCameraDiagnosticsModel.h"

#include "ACFCameraSequence.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraTuningPanelModel.h"
#include "Camera/PlayerCameraManager.h"
#include "Debug/DebugDrawService.h"
#include "Editor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace ACFDiagnosticsImpl
{
	FString Vec(const FVector& V) { return FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), V.X, V.Y, V.Z); }
	FString Rot(const FRotator& R) { return FString::Printf(TEXT("(P %.2f, Y %.2f, R %.2f)"), R.Pitch, R.Yaw, R.Roll); }
	FString Stats(const FACFCameraProfileStats& S) { return FString::Printf(TEXT("mean %.2f  P95 %.2f  max %.2f  (n=%d)"), S.Mean, S.P95, S.Max, S.Samples); }
	const TCHAR* Resolution(EACFViewTargetResolution R)
	{
		switch (R)
		{
		case EACFViewTargetResolution::None: return TEXT("none (no output pass yet)");
		case EACFViewTargetResolution::POVAddress: return TEXT("standard PCM (exact)");
		case EACFViewTargetResolution::Explicit: return TEXT("explicit (exact)");
		case EACFViewTargetResolution::SingleViewTarget: return TEXT("single view target");
		case EACFViewTargetResolution::SameDecision: return TEXT("view target blend (same decision)");
		default: return TEXT("view target blend, unknown target: ACF does not write");
		}
	}

	/** The player's camera manager, subsystem and on-screen view target (null in test-director mode). */
	struct FPlayerView
	{
		APlayerController* PC = nullptr;
		APlayerCameraManager* PCM = nullptr;
		UACFCameraSubsystem* Subsystem = nullptr;
	};

	FPlayerView ResolvePlayer(const FACFCameraTuningPanelModel& Target)
	{
		FPlayerView View;
		ULocalPlayer* Player = Target.GetPlayer();
		UWorld* World = Target.GetWorld();
		if (Player && World)
		{
			View.PC = Player->GetPlayerController(World);
			View.PCM = View.PC ? View.PC->PlayerCameraManager.Get() : nullptr;
			View.Subsystem = Player->GetSubsystem<UACFCameraSubsystem>();
		}
		return View;
	}

	/** What the screen shows: ACF's pose, a blend with the UE camera, or another camera. */
	void DescribeOutput(const FACFCameraEvaluationResult& Result, const FPlayerView& Player, bool& bOutAcfOnScreen, FString& OutState, FString& OutViewTarget)
	{
		const AActor* ViewTarget = Player.PCM ? Player.PCM->GetViewTarget() : nullptr;
		OutViewTarget = ViewTarget ? ViewTarget->GetActorNameOrLabel() : TEXT("-");
		const bool bExternal = Player.Subsystem && ViewTarget && Player.Subsystem->IsExternalViewTarget(ViewTarget);
		bOutAcfOnScreen = false;
		if (!Player.PCM)
		{
			OutState = TEXT("no player camera manager (not a PIE player): the screen is not known");
		}
		else if (bExternal || Result.bOutputBypassed)
		{
			OutState = FString::Printf(TEXT("NOT ACF: external view target %s is on screen (e.g. a Sequencer camera cut); ACF evaluates in the background"), *OutViewTarget);
		}
		else if (Result.bOutputSuppressed && Result.Weight <= 0.f)
		{
			OutState = TEXT("NOT ACF: output suppressed (e.g. Suppress Output While Playing)");
		}
		else if (Result.Weight <= 0.f)
		{
			OutState = TEXT("NOT ACF: no request in control, the UE camera is on screen");
		}
		else if (Result.Weight < 1.f)
		{
			OutState = FString::Printf(TEXT("blend of ACF and the UE camera (weight %.3f%s)"), Result.Weight, Result.bOutputSuppressed ? TEXT(", suppression fading") : TEXT(""));
		}
		else
		{
			bOutAcfOnScreen = true;
			OutState = TEXT("ACF on screen (weight 1; effects and UE shakes come on top)");
		}
		if (Player.PCM && Player.PCM->PendingViewTarget.Target)
		{
			OutState += FString::Printf(TEXT("; PCM view target blend to %s in progress"), *Player.PCM->PendingViewTarget.Target->GetActorNameOrLabel());
			bOutAcfOnScreen = false;
		}
	}
}

FACFCameraDiagnosticsModel::FACFCameraDiagnosticsModel(FACFCameraTuningPanelModel& InTarget)
	: Target(InTarget)
{
	PrePIEEndedHandle = FEditorDelegates::PrePIEEnded.AddRaw(this, &FACFCameraDiagnosticsModel::OnPrePIEEnded);
}

FACFCameraDiagnosticsModel::~FACFCameraDiagnosticsModel()
{
	StopProfiling();
	SetGuidesVisible(false);
	FEditorDelegates::PrePIEEnded.Remove(PrePIEEndedHandle);
}

UACFCameraDirector* FACFCameraDiagnosticsModel::ResolveDirector() const
{
	return Target.ResolveDirector();
}

FString FACFCameraDiagnosticsModel::DescribeTarget() const
{
	const UWorld* World = Target.GetWorld();
	const ULocalPlayer* Player = Target.GetPlayer();
	const UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return TEXT("no target (start PIE and select a world / player)");
	}
	if (!World || !Player)
	{
		return FString::Printf(TEXT("director %s (test)"), *Director->GetName());
	}
	const int32 Index = World->GetGameInstance() ? World->GetGameInstance()->GetLocalPlayers().IndexOfByKey(Player) : INDEX_NONE;
	return FString::Printf(TEXT("%s / Player %d"), *World->GetMapName(), Index);
}

void FACFCameraDiagnosticsModel::OnPrePIEEnded(bool bIsSimulating)
{
	StopProfiling(); // the director goes away with the PIE world; nothing may stay forced or referenced
	ClearBaseline();
}

void FACFCameraDiagnosticsModel::Validate()
{
	if (ProfiledDirector.IsStale() || (ProfiledDirector.IsValid() && ProfiledDirector.Get() != ResolveDirector()))
	{
		StopProfiling();
	}
}

// --- Profiler ----------------------------------------------------------------------------------------------------------

bool FACFCameraDiagnosticsModel::StartProfiling()
{
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return false;
	}
	if (ProfiledDirector.IsValid() && ProfiledDirector.Get() != Director)
	{
		StopProfiling();
	}
	Director->SetProfilerForced(true); // recording starts with the next evaluation (B-1: allocates the ring buffer once)
	ProfiledDirector = Director;
	Profile = FProfileView();
	Profile.bForcedByPanel = true;
	return true;
}

void FACFCameraDiagnosticsModel::StopProfiling()
{
	if (UACFCameraDirector* Director = ProfiledDirector.Get())
	{
		const bool bWasPaused = bPaused;
		bPaused = false;
		UpdateProfile(); // keep the last statistics on screen: the director frees its buffer when nothing else wants it
		bPaused = bWasPaused;
		Director->SetProfilerForced(false);
	}
	ProfiledDirector.Reset();
	Profile.bForcedByPanel = false;
	Profile.bRecording = false;
}

void FACFCameraDiagnosticsModel::ResetStatistics()
{
	if (UACFCameraDirector* Director = ResolveDirector())
	{
		if (Director->GetProfiler().IsRecording())
		{
			Director->GetProfiler().Clear();
		}
	}
	TuningChangeFrame = 0;
	const bool bForced = Profile.bForcedByPanel;
	Profile = FProfileView();
	Profile.bForcedByPanel = bForced;
}

bool FACFCameraDiagnosticsModel::IsProfiling() const
{
	const UACFCameraDirector* Director = ResolveDirector();
	return Director && Director->GetProfiler().IsRecording();
}

const FACFCameraDiagnosticsModel::FProfileView& FACFCameraDiagnosticsModel::UpdateProfile()
{
	if (bPaused)
	{
		return Profile;
	}
	UACFCameraDirector* Director = ResolveDirector();
	const FACFCameraProfiler* Profiler = Director ? &Director->GetProfiler() : nullptr;
	if (!Profiler || !Profiler->IsRecording())
	{
		Profile.bRecording = false; // the last statistics stay as they were
		return Profile;
	}
	TArray<const FACFCameraProfileFrame*> Frames;
	Profiler->GetFrames(Frames);
	FProfileView View;
	View.bValid = Frames.Num() > 0;
	View.bRecording = true;
	View.bForcedByPanel = ProfiledDirector.IsValid() && ProfiledDirector.Get() == Director;
	View.Target = DescribeTarget();
	View.Capacity = Profiler->Capacity();
	if (Frames.Num() > 0)
	{
		View.FirstFrame = Frames[0]->FrameNumber;
		View.LastFrame = Frames.Last()->FrameNumber;
		for (const FACFCameraProfileFrame* Frame : Frames)
		{
			View.Seconds += Frame->DeltaTime;
		}
		View.Summary = FACFCameraProfileSummary::Summarize(Frames);
		for (const TPair<FName, FACFCameraProfileStats>& Node : View.Summary.NodeUs)
		{
			FNodeRow& Row = View.Nodes.AddDefaulted_GetRef();
			Row.NodeClass = Node.Key;
			Row.Us = Node.Value;
			Row.Share = View.Summary.TotalUs.Mean > 0.0 ? Node.Value.Mean * Node.Value.Samples / (View.Summary.TotalUs.Mean * View.Summary.NumFrames) : 0.0;
		}
		View.Nodes.Sort([](const FNodeRow& A, const FNodeRow& B) { return A.Us.Mean > B.Us.Mean; });
		// Heaviest leaf section (Evaluate / PostBlend contain others) and heaviest node.
		const EACFCameraProfileSection Leaves[] = { EACFCameraProfileSection::SequenceTick, EACFCameraProfileSection::ResolveRequests, EACFCameraProfileSection::RigEvaluate,
			EACFCameraProfileSection::Effects, EACFCameraProfileSection::Deocclude, EACFCameraProfileSection::FramingRecovery, EACFCameraProfileSection::PreEvaluate,
			EACFCameraProfileSection::BackgroundUpstream, EACFCameraProfileSection::OutputGuard };
		EACFCameraProfileSection Top = Leaves[0];
		for (const EACFCameraProfileSection Section : Leaves)
		{
			if (View.Summary.SectionUs[static_cast<int32>(Section)].Mean > View.Summary.SectionUs[static_cast<int32>(Top)].Mean)
			{
				Top = Section;
			}
		}
		View.Hotspot = FString::Printf(TEXT("section %s (mean %.2f us)"), LexToString(Top), View.Summary.SectionUs[static_cast<int32>(Top)].Mean);
		if (View.Nodes.Num() > 0)
		{
			View.Hotspot += FString::Printf(TEXT(", node %s (mean %.2f us)"), *View.Nodes[0].NodeClass.ToString(), View.Nodes[0].Us.Mean);
		}
	}
#if ACF_WITH_PROFILER
	View.bAllocationsCounted = FACFCameraAllocationCounter::IsCounting();
#endif
	View.TuningChangeFrame = TuningChangeFrame;
	View.bMixesTuningChange = TuningChangeFrame != 0 && View.bValid && TuningChangeFrame > View.FirstFrame && TuningChangeFrame <= View.LastFrame;
	Profile = MoveTemp(View);
	return Profile;
}

// --- Debugger ----------------------------------------------------------------------------------------------------------

const FACFCameraDiagnosticsModel::FRow* FACFCameraDiagnosticsModel::FindRow(const FDebugView& View, const TCHAR* Label)
{
	return View.Rows.FindByPredicate([Label](const FRow& Row) { return Row.Label == Label; });
}

FACFCameraDiagnosticsModel::FDebugView FACFCameraDiagnosticsModel::ReadDebug() const
{
	using namespace ACFDiagnosticsImpl;
	FDebugView View;
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return View;
	}
	View.bValid = true;
	auto Add = [&View](const TCHAR* Section, const TCHAR* Label, FString Value) { View.Rows.Add({ Section, Label, MoveTemp(Value) }); };
	const FPlayerView Player = ResolvePlayer(Target);
	View.Result = Director->GetLastResult();
	View.Transition = Director->GetTransitionInfo();
	View.Health = Director->GetHealthDiagnostics();
	const FACFCameraEvaluationResult& R = View.Result;

	// Output: what is on screen.
	DescribeOutput(R, Player, View.bAcfOnScreen, View.OutputState, View.ViewTarget);
	Add(TEXT("Output"), TEXT("Target"), DescribeTarget());
	Add(TEXT("Output"), TEXT("On screen"), View.OutputState);
	Add(TEXT("Output"), TEXT("View target"), View.ViewTarget);
	if (Player.PCM)
	{
		View.bHasRenderedView = true;
		View.RenderedLocation = Player.PCM->GetCameraLocation();
		View.RenderedRotation = Player.PCM->GetCameraRotation();
		View.RenderedFov = Player.PCM->GetFOVAngle();
		Add(TEXT("Output"), TEXT("Camera manager"), Player.PCM->GetClass()->GetName());
		Add(TEXT("Output"), TEXT("Rendered pose"), FString::Printf(TEXT("%s %s FOV %.2f"), *Vec(View.RenderedLocation), *Rot(View.RenderedRotation), View.RenderedFov));
	}
	if (Player.Subsystem)
	{
		const FACFCameraOutputDiagnostics Output = Player.Subsystem->GetOutputDiagnostics();
		Add(TEXT("Output"), TEXT("Output pass"), FString::Printf(TEXT("%s, frame %lld"), Resolution(Output.LastResolution), Output.Frame));
	}
	Add(TEXT("Output"), TEXT("Suppression"), FString::Printf(TEXT("%d source(s), fade %.2f -> %.2f over %.2f s%s"), Director->GetNumOutputSuppressions(),
		View.Transition.SuppressionValue, View.Transition.SuppressionTarget, View.Transition.SuppressionDuration, R.bOutputBypassed ? TEXT(", output bypassed (external view target)") : TEXT("")));

	// Requests.
	TArray<FACFCameraRequestInfo> Requests;
	Director->GetRequestInfos(Requests);
	const FACFCameraRequestInfo* Active = Requests.FindByPredicate([&R](const FACFCameraRequestInfo& Info) { return Info.Handle == R.ActiveRequest; });
	Add(TEXT("Request"), TEXT("Active request"), Active ? FString::Printf(TEXT("%s%s"), *Active->Handle.ToString(), Active->DebugName.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Active->DebugName.ToString()))
		: TEXT("none (the UE camera)"));
	Add(TEXT("Request"), TEXT("Priority"), Active ? FString::FromInt(Active->Priority) : TEXT("-"));
	Add(TEXT("Request"), TEXT("Preset"), Active ? FString::Printf(TEXT("%s%s"), *Active->Preset.ToString(), Active->bTuning ? TEXT(" (tuning copy)") : TEXT("")) : TEXT("-"));
	Add(TEXT("Request"), TEXT("Weight"), FString::Printf(TEXT("output %.3f, requests %.3f"), R.Weight, R.RequestWeight));
	Add(TEXT("Request"), TEXT("Requests"), FString::Printf(TEXT("%d"), R.NumRequests));
	for (const FACFCameraRequestInfo& Info : Requests)
	{
		Add(TEXT("Request"), TEXT("  request"), FString::Printf(TEXT("%s pri %d %s%s%s  follow [%s]  look at [%s]"), *Info.Handle.ToString(), Info.Priority, *Info.Preset.ToString(),
			Info.bActive ? TEXT(" ACTIVE") : TEXT(""), Info.bTuning ? TEXT(" TUNING") : TEXT(""), *Info.FollowTarget, *Info.LookAtTarget));
	}
	const FACFCameraTransitionInfo& T = View.Transition;
	Add(TEXT("Blend"), TEXT("Blend"), T.bActive ? FString::Printf(TEXT("blending, alpha %.3f, %.2f / %.2f s (%.2f s left), from %s, weight %.2f -> %.2f"), R.BlendAlpha, T.Elapsed, T.Duration,
		FMath::Max(0.f, T.Duration - T.Elapsed), T.From.IsValid() ? *T.From.ToString() : TEXT("snapshot / UE camera"), T.FromWeight, T.ToWeight) : TEXT("not blending"));
	Add(TEXT("Pose"), TEXT("ACF pose"), FString::Printf(TEXT("%s %s FOV %.2f"), *Vec(R.Pose.Location), *Rot(R.Pose.Rotation), R.Pose.FieldOfView));
	Add(TEXT("Pose"), TEXT("UE camera (upstream)"), FString::Printf(TEXT("%s %s FOV %.2f"), *Vec(R.UpstreamPose.Location), *Rot(R.UpstreamPose.Rotation), R.UpstreamPose.FieldOfView));
	Add(TEXT("Pose"), TEXT("Composition lens"), FString::Printf(TEXT("view aspect %.4f, horizontal FOV scale %.5f: pose FOV %.2f is framed as %.3f (the renderer's projection; ACF.Projection.RenderedLens)"),
		R.AspectRatio, R.HorizontalFovScale, R.Pose.FieldOfView, FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(R.Pose.FieldOfView) * 0.5) * R.HorizontalFovScale))));
	if (View.bHasRenderedView)
	{
		Add(TEXT("Pose"), TEXT("Rendered - ACF"), FString::Printf(TEXT("%.2f cm, %.3f deg, FOV %+.2f (effects / shakes / blends / another camera)"),
			FVector::Dist(View.RenderedLocation, R.Pose.Location), (View.RenderedRotation - R.Pose.Rotation).GetNormalized().Euler().GetAbsMax(), View.RenderedFov - R.Pose.FieldOfView));
	}

#if !UE_BUILD_SHIPPING
	const FACFCameraRigDebug& D = Director->GetLastDebug();
	if (D.bHasLockOn)
	{
		Add(TEXT("Rig"), TEXT("Lock-On"), FString::Printf(TEXT("target point %s, focus %s, range %.0f%s"), *Vec(D.LockOnPoint), *Vec(D.LockOnFocus), D.LockOnRange, D.bLockOnTargetLost ? TEXT(", TARGET LOST") : TEXT("")));
	}
	if (D.bHasGroup)
	{
		Add(TEXT("Rig"), TEXT("Group Framing"), FString::Printf(TEXT("%d subject(s), center %s, dolly %.1f, FOV %+.2f, size (%.2f, %.2f)%s"), D.NumGroupSubjects, *Vec(D.GroupCenter), D.GroupDolly,
			D.GroupFovDelta, D.GroupFramingSize.X, D.GroupFramingSize.Y, R.bFramingLimited ? TEXT(", LIMITED (some may be off screen)") : TEXT("")));
	}
	if (D.bHasCombat)
	{
		Add(TEXT("Rig"), TEXT("Combat"), FString::Printf(TEXT("%s weight %.2f, distance x%.2f, FOV %+.2f"), D.bInCombat ? TEXT("in combat") : TEXT("out of combat"), D.CombatWeight, D.CombatDistanceScale, D.CombatFovOffset));
	}
	if (D.bHasCollision)
	{
		Add(TEXT("Rig"), TEXT("Collision"), D.bCollisionHit ? FString::Printf(TEXT("HIT: pulled in %.1f cm (desired %s, now %s)"), FVector::Dist(D.CollisionDesired, D.CollisionHit), *Vec(D.CollisionDesired), *Vec(D.CollisionHit))
			: TEXT("no hit (camera where the rig wants it)"));
	}
	if (D.bHasFollowTarget)
	{
		Add(TEXT("Rig"), TEXT("Follow target"), Vec(D.FollowTarget));
	}
	if (D.bHasLookAtTarget)
	{
		Add(TEXT("Rig"), TEXT("Look-at target"), Vec(D.LookAtTarget));
	}
	if (D.bHasComposition)
	{
		Add(TEXT("Rig"), TEXT("Composer"), FString::Printf(TEXT("target on screen (%.3f, %.3f), aim (%.3f, %.3f), dead zone (%.2f, %.2f)"), D.TargetOnScreen.X, D.TargetOnScreen.Y,
			D.ScreenPosition.X, D.ScreenPosition.Y, D.DeadZone.X, D.DeadZone.Y));
	}
	// Phase 7B-1: camera assistance of the camera in control (Orbital Follow recenter / Movement Assistance). Formatted here, at
	// UI rate, from the plain values the node published; the evaluation itself builds no string.
	{
		auto EnumText = [](const UEnum* Enum, int64 Value) { return Enum->GetDisplayNameTextByValue(Value).ToString(); };
		const int32 NumSources = Director->GetNumAssistanceSuppressions();
		const FString Suppression = NumSources > 0 ? FString::Printf(TEXT("suppressed by %d source(s)"), NumSources) : FString(TEXT("not suppressed"));
		if (D.bHasAssistance)
		{
			Add(TEXT("Assistance"), TEXT("Assistance mode"), D.bAssistanceEnabled ? TEXT("Movement Assistance") : TEXT("Idle recenter (Recenter When Idle, Phase 2)"));
			Add(TEXT("Assistance"), TEXT("Assistance state"), EnumText(StaticEnum<EACFCameraAssistanceState>(), static_cast<int64>(D.AssistanceState)));
			Add(TEXT("Assistance"), TEXT("Heading source"), EnumText(StaticEnum<EACFCameraRecenterHeading>(), static_cast<int64>(D.AssistanceHeading)));
			Add(TEXT("Assistance"), TEXT("Target yaw"), D.bHasAssistanceTargetYaw ? FString::Printf(TEXT("%.2f deg"), D.AssistanceTargetYaw) : FString(TEXT("- (not moving)")));
			Add(TEXT("Assistance"), TEXT("Movement speed"), D.bAssistanceEnabled ? FString::Printf(TEXT("%.1f cm/s (moving %.2f / %.2f s)"), D.AssistanceMovementSpeed,
				D.AssistanceMovementTime, D.AssistanceRequiredMovementTime) : FString(TEXT("- (not measured without Movement Assistance)")));
			Add(TEXT("Assistance"), TEXT("Waiting time"), FString::Printf(TEXT("%.2f / %.2f s"), D.AssistanceWaitTime, D.AssistanceRecenterDelay));
			Add(TEXT("Assistance"), TEXT("Assistance suppression"), Suppression);
			Add(TEXT("Assistance"), TEXT("Not assisting because"), D.AssistanceReason == EACFCameraAssistanceReason::None ? FString(TEXT("-"))
				: EnumText(StaticEnum<EACFCameraAssistanceReason>(), static_cast<int64>(D.AssistanceReason)));
		}
		else
		{
			Add(TEXT("Assistance"), TEXT("Assistance mode"), FString::Printf(TEXT("none (no Orbital Follow with recenter in the camera in control), %s"), *Suppression));
		}
	}
#endif
	const FACFCameraSolverReport& S = Director->GetLastSolverReport();
	Add(TEXT("Rig"), TEXT("Deoccluder"), FString::Printf(TEXT("%d checked, %d hidden%s%s, correction %.1f cm, FOV recovery %.2f, %d trace(s)"), S.NumCheckedSubjects, S.NumOccluded,
		S.bPlayerOccluded ? TEXT(", PLAYER HIDDEN") : TEXT(""), S.bSearched ? TEXT(", searched") : TEXT(""), S.Correction.Size(), S.FovRecovery, S.NumTraces));

	// Nodes of the active request and what they did (from the rig debug and, while profiling, their time).
	if (Active)
	{
		TArray<FACFCameraTuningNodeInfo> Nodes;
		Director->GetRequestNodes(Active->Handle, Nodes);
		for (const FACFCameraTuningNodeInfo& Node : Nodes)
		{
			const FNodeRow* Timed = Profile.Nodes.FindByPredicate([&Node](const FNodeRow& Row) { return Row.NodeClass == Node.NodeClass; });
			Add(TEXT("Nodes"), TEXT("  node"), FString::Printf(TEXT("#%d %s %s%s"), Node.NodeIndex, *Node.NodeClass.ToString(), Node.bEnabled ? TEXT("") : TEXT("(disabled) "),
				Timed ? *FString::Printf(TEXT("mean %.2f us"), Timed->Us.Mean) : TEXT("")));
		}
	}

	TArray<FACFCameraEffectInfo> Effects;
	Director->GetActiveEffects(Effects);
	Add(TEXT("Effects"), TEXT("Effects"), FString::FromInt(Effects.Num()));
	for (const FACFCameraEffectInfo& Effect : Effects)
	{
		Add(TEXT("Effects"), TEXT("  effect"), FString::Printf(TEXT("%s %s t %.2f weight %.2f layers %d%s"), *Effect.Handle.ToString(), *Effect.PresetName.ToString(), Effect.Time, Effect.Weight,
			Effect.ActiveLayers, Effect.bStopping ? TEXT(" stopping") : TEXT("")));
	}
	TArray<FACFCameraSequenceHandle> Sequences;
	Director->GetActiveSequences(Sequences);
	Add(TEXT("Sequences"), TEXT("Camera sequences"), FString::FromInt(Sequences.Num()));
	for (const FACFCameraSequenceHandle Sequence : Sequences)
	{
		FACFCameraSequenceState State;
		if (Director->GetSequenceState(Sequence, State))
		{
			Add(TEXT("Sequences"), TEXT("  sequence"), FString::Printf(TEXT("%s shot %d/%d '%s' %.2f/%.2f s%s"), *Sequence.ToString(), State.ShotIndex, State.NumShots, *State.ShotName.ToString(),
				State.ShotTime, State.ShotDuration, State.bInControl ? TEXT("") : TEXT(" (not in control)")));
		}
	}

	const FACFCameraHealthDiagnostics& H = View.Health;
	Add(TEXT("Health"), TEXT("Health issues"), FString::Printf(TEXT("%d (targets %d, delta time %d, UE camera %d, inputs %d, nodes %d, effects %d, final %d)"), H.TotalIssues, H.InvalidTargets,
		H.InvalidDeltaTimes, H.InvalidUpstreamPoses, H.RejectedInputs, H.InvalidNodeOutputs, H.InvalidEffectOutputs, H.InvalidFinalPoses));
	Add(TEXT("Health"), TEXT("Last issue"), H.LastIssue.IsEmpty() ? FString(TEXT("-")) : FString::Printf(TEXT("frame %lld: %s"), H.LastIssueFrame, *H.LastIssue));
	return View;
}

// --- Composition -------------------------------------------------------------------------------------------------------

bool FACFCameraDiagnosticsModel::ProjectPoint(const FMatrix& ViewProjection, const FIntRect& ViewRect, const FVector& World, FVector2D& OutPixel, FVector2D& OutNdc)
{
	const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(World, 1.0));
	if (Clip.W <= UE_SMALL_NUMBER)
	{
		return false;
	}
	OutNdc = FVector2D(Clip.X / Clip.W, Clip.Y / Clip.W);
	OutPixel = FVector2D((OutNdc.X * 0.5 + 0.5) * ViewRect.Width(), (0.5 - OutNdc.Y * 0.5) * ViewRect.Height());
	return true;
}

FACFCameraDiagnosticsModel::FCompositionView FACFCameraDiagnosticsModel::ReadComposition() const
{
	using namespace ACFDiagnosticsImpl;
	FCompositionView View;
	ULocalPlayer* Player = Target.GetPlayer();
	UWorld* World = Target.GetWorld();
	UACFCameraDirector* Director = ResolveDirector();
	if (!Player || !World || !Director)
	{
		View.Reason = TEXT("no PIE player selected");
		return View;
	}
	UGameViewportClient* ViewportClient = World->GetGameViewport();
	FViewport* Viewport = ViewportClient ? ViewportClient->Viewport : nullptr;
	if (!Viewport || Viewport->GetSizeXY().X <= 0 || Viewport->GetSizeXY().Y <= 0)
	{
		View.Reason = TEXT("the PIE viewport has no size (not rendered)");
		return View;
	}
	FSceneViewProjectionData Projection;
	if (!Player->GetProjectionData(Viewport, Projection))
	{
		View.Reason = TEXT("the player has no view this frame");
		return View;
	}
	View.bValid = true;
	View.ViewportSize = Viewport->GetSizeXY();
	View.ViewRect = Projection.GetConstrainedViewRect();
	View.ViewProjection = Projection.ComputeViewProjectionMatrix();
	View.ViewOrigin = Projection.ViewOrigin;
	View.ProjectionFov = Projection.ProjectionMatrix.M[0][0] > 0.0 ? static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Projection.ProjectionMatrix.M[0][0]))) : 0.f;
	View.ViewRotation = Projection.ViewRotationMatrix.Rotator();
	View.AspectRatio = View.ViewRect.Height() > 0 ? static_cast<float>(View.ViewRect.Width()) / View.ViewRect.Height() : 0.f;
	// Perspective (MaintainXFOV): M[1][1] / M[0][0] is the aspect the renderer projects with.
	View.ProjectionAspect = Projection.ProjectionMatrix.M[0][0] != 0.0 ? static_cast<float>(Projection.ProjectionMatrix.M[1][1] / Projection.ProjectionMatrix.M[0][0]) : 0.f;
	const FPlayerView PlayerView = ResolvePlayer(Target);
	View.Fov = PlayerView.PCM ? PlayerView.PCM->GetFOVAngle() : 0.f;
	DescribeOutput(Director->GetLastResult(), PlayerView, View.bAcfOnScreen, View.OutputState, View.ViewTarget);

	auto AddPoint = [&View](FString Label, const FVector& World)
	{
		FProjectedPoint& Point = View.Points.AddDefaulted_GetRef();
		Point.Label = MoveTemp(Label);
		Point.World = World;
		Point.bInFront = ProjectPoint(View.ViewProjection, View.ViewRect, World, Point.Pixel, Point.Ndc);
		Point.bOnScreen = Point.bInFront && FMath::Abs(Point.Ndc.X) <= 1.0 && FMath::Abs(Point.Ndc.Y) <= 1.0;
	};
	if (const APawn* Pawn = PlayerView.PC ? PlayerView.PC->GetPawn() : nullptr)
	{
		AddPoint(TEXT("Player pawn"), Pawn->GetActorLocation());
	}
#if !UE_BUILD_SHIPPING
	const FACFCameraRigDebug& D = Director->GetLastDebug();
	if (D.bHasFollowTarget) { AddPoint(TEXT("Follow target"), D.FollowTarget); }
	if (D.bHasLookAtTarget) { AddPoint(TEXT("Look-at target"), D.LookAtTarget); }
	if (D.bHasLockOn)
	{
		AddPoint(TEXT("Lock-on target"), D.LockOnPoint);
		AddPoint(TEXT("Lock-on focus"), D.LockOnFocus);
	}
	if (D.bHasGroup)
	{
		AddPoint(TEXT("Group center"), D.GroupCenter);
		for (int32 Index = 0; Index < D.NumGroupSubjects && Index < FACFCameraRigDebug::MaxGroupSubjects; ++Index)
		{
			AddPoint(FString::Printf(TEXT("Group subject %d"), Index), FVector(D.GroupSubjects[Index]));
		}
	}
#endif
	return View;
}

void FACFCameraDiagnosticsModel::SetGuidesVisible(bool bVisible)
{
	if (bVisible && !DrawHandle.IsValid())
	{
		DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateRaw(this, &FACFCameraDiagnosticsModel::DrawGuides));
	}
	else if (!bVisible && DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}
}

void FACFCameraDiagnosticsModel::DrawGuides(UCanvas* Canvas, APlayerController* PlayerController)
{
	// Only the target player's view (split screen: one call per player view, the canvas is that view's rectangle).
	const ULocalPlayer* Player = Target.GetPlayer();
	UWorld* World = Target.GetWorld();
	if (!Canvas || !Player || !World || !PlayerController || PlayerController != Player->GetPlayerController(World) || !Canvas->SceneView)
	{
		return;
	}
	++NumGuideDraws;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	LastGuideCanvasSize = FIntPoint(FMath::RoundToInt(W), FMath::RoundToInt(H));
	const FLinearColor Grid(1.f, 1.f, 1.f, 0.35f);
	auto Line = [Canvas](float X0, float Y0, float X1, float Y1, const FLinearColor& Color) { Canvas->K2_DrawLine(FVector2D(X0, Y0), FVector2D(X1, Y1), 1.f, Color); };
	auto Rect = [&Line](float Fraction, float W, float H, const FLinearColor& Color)
	{
		const float X0 = W * (1.f - Fraction) * 0.5f, Y0 = H * (1.f - Fraction) * 0.5f, X1 = W - X0, Y1 = H - Y0;
		Line(X0, Y0, X1, Y0, Color); Line(X1, Y0, X1, Y1, Color); Line(X1, Y1, X0, Y1, Color); Line(X0, Y1, X0, Y0, Color);
	};
	for (int32 I = 1; I <= 2; ++I)
	{
		Line(W * I / 3.f, 0.f, W * I / 3.f, H, Grid); // rule of thirds
		Line(0.f, H * I / 3.f, W, H * I / 3.f, Grid);
	}
	Line(W * 0.5f - 12.f, H * 0.5f, W * 0.5f + 12.f, H * 0.5f, FLinearColor::White); // center
	Line(W * 0.5f, H * 0.5f - 12.f, W * 0.5f, H * 0.5f + 12.f, FLinearColor::White);
	Rect(0.9f, W, H, FLinearColor(1.f, 0.8f, 0.f, 0.6f)); // action safe 90 %
	Rect(0.8f, W, H, FLinearColor(1.f, 0.4f, 0.f, 0.6f)); // title safe 80 %

	// Targets projected with the matrices this very view is rendered with.
	const FMatrix ViewProjection = Canvas->SceneView->ViewMatrices.GetWorldToClip();
	const FIntRect CanvasRect(0, 0, FMath::RoundToInt(W), FMath::RoundToInt(H));
	const FCompositionView Composition = ReadComposition();
	LastGuidePoints.Reset();
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	for (const FProjectedPoint& Point : Composition.Points)
	{
		FVector2D Pixel, Ndc;
		if (!ProjectPoint(ViewProjection, CanvasRect, Point.World, Pixel, Ndc))
		{
			continue;
		}
		LastGuidePoints.Emplace(Point.Label, Ndc);
		const bool bOn = FMath::Abs(Ndc.X) <= 1.0 && FMath::Abs(Ndc.Y) <= 1.0;
		const FLinearColor Color = bOn ? FLinearColor::Green : FLinearColor::Red;
		Pixel.X = FMath::Clamp(Pixel.X, 4.0, W - 4.0);
		Pixel.Y = FMath::Clamp(Pixel.Y, 4.0, H - 4.0);
		Line(Pixel.X - 6.f, Pixel.Y - 6.f, Pixel.X + 6.f, Pixel.Y + 6.f, Color);
		Line(Pixel.X - 6.f, Pixel.Y + 6.f, Pixel.X + 6.f, Pixel.Y - 6.f, Color);
		if (Font)
		{
			Canvas->SetDrawColor(Color.ToFColor(true));
			Canvas->DrawText(Font, Point.Label, Pixel.X + 8.f, Pixel.Y - 6.f);
		}
	}
	if (Font)
	{
		Canvas->SetDrawColor(Composition.bAcfOnScreen ? FColor::Green : FColor::Orange);
		Canvas->DrawText(Font, FString::Printf(TEXT("ACF composition (editor only): %s"), *Composition.OutputState), 12.f, 12.f);
	}
}

// --- Comparison --------------------------------------------------------------------------------------------------------

void FACFCameraDiagnosticsModel::CaptureBaseline()
{
	const FDebugView Debug = ReadDebug();
	Baseline = FBaseline();
	if (!Debug.bValid)
	{
		return;
	}
	Baseline.bValid = true;
	Baseline.Target = DescribeTarget();
	Baseline.Frame = Debug.Result.FrameNumber;
	Baseline.AcfPose = Debug.Result.Pose;
	Baseline.RenderedLocation = Debug.RenderedLocation;
	Baseline.RenderedRotation = Debug.RenderedRotation;
	Baseline.RenderedFov = Debug.RenderedFov;
	if (Profile.bValid)
	{
		Baseline.TotalUs = Profile.Summary.TotalUs;
		Baseline.FirstFrame = Profile.FirstFrame;
		Baseline.LastFrame = Profile.LastFrame;
	}
}

TArray<FString> FACFCameraDiagnosticsModel::DescribeComparison() const
{
	using namespace ACFDiagnosticsImpl;
	TArray<FString> Lines;
	if (!Baseline.bValid)
	{
		return Lines;
	}
	const FDebugView Now = ReadDebug();
	if (!Now.bValid)
	{
		Lines.Add(TEXT("Baseline kept, but the target is gone."));
		return Lines;
	}
	const FACFCameraPose& A = Baseline.AcfPose;
	const FACFCameraPose& B = Now.Result.Pose;
	Lines.Add(FString::Printf(TEXT("A = frame %llu (%s), B = frame %llu (now)"), Baseline.Frame, *Baseline.Target, Now.Result.FrameNumber));
	Lines.Add(FString::Printf(TEXT("ACF pose  A %s %s FOV %.2f"), *Vec(A.Location), *Rot(A.Rotation), A.FieldOfView));
	Lines.Add(FString::Printf(TEXT("ACF pose  B %s %s FOV %.2f"), *Vec(B.Location), *Rot(B.Rotation), B.FieldOfView));
	Lines.Add(FString::Printf(TEXT("B - A: %.2f cm, rotation %s, FOV %+.2f"), FVector::Dist(A.Location, B.Location), *Rot((B.Rotation - A.Rotation).GetNormalized()), B.FieldOfView - A.FieldOfView));
	if (Baseline.TotalUs.Samples > 0 && Profile.bValid)
	{
		Lines.Add(FString::Printf(TEXT("ACF time A: %s over frames %llu..%llu"), *Stats(Baseline.TotalUs), Baseline.FirstFrame, Baseline.LastFrame));
		Lines.Add(FString::Printf(TEXT("ACF time B: %s over frames %llu..%llu%s"), *Stats(Profile.Summary.TotalUs), Profile.FirstFrame, Profile.LastFrame,
			Profile.FirstFrame <= Baseline.LastFrame ? TEXT("  (periods overlap: Reset Statistics after the change to compare cleanly)") : TEXT("")));
	}
	return Lines;
}

void FACFCameraDiagnosticsModel::NotifyTuningChanged()
{
	// In the director's frame numbers (what the profile frames carry): the next evaluation is the first one "after".
	const UACFCameraDirector* Director = ResolveDirector();
	TuningChangeFrame = (Director ? Director->GetLastResult().FrameNumber : GFrameCounter) + 1;
}
