#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraTuningPanelModel.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "SACFCameraTuningPanel.h"
#include "Serialization/ObjectWriter.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/** Phase 7A-2: the editor tuning panel (model, widget handlers, real PIE). README 25 章. */
namespace ACFTuningPanelTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using EParam = EACFCameraTuningParameter;
	using EResult = EACFCameraTuningResult;

	inline UACFCameraPreset* MakePreset()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	/** Two Follow nodes (same class), a FOV-only Fixed Pose and a Collision. */
	inline UACFCameraPreset* MakeTwoFollowPreset()
	{
		UACFCameraPreset* Preset = MakePreset();
		UACFCameraNode_Follow* A = NewObject<UACFCameraNode_Follow>(Preset);
		A->Offset = FVector(-300.0, 0.0, 150.0);
		UACFCameraNode_Follow* B = NewObject<UACFCameraNode_Follow>(Preset);
		B->Offset = FVector(-400.0, 50.0, 100.0);
		UACFCameraNode_FixedPose* Lens = NewObject<UACFCameraNode_FixedPose>(Preset);
		Lens->bOverrideLocation = false;
		Lens->bOverrideRotation = false;
		Lens->FieldOfView = 80.f;
		UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
		Preset->Nodes = { A, B, Lens, Collision };
		return Preset;
	}

	inline USceneComponent* MakeTarget()
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		return Component;
	}

	inline FACFCameraRequestParams FollowParams(USceneComponent* Target, int32 Priority = 0)
	{
		FACFCameraRequestParams Params;
		Params.FollowTarget.Component = Target;
		Params.Priority = Priority;
		return Params;
	}

	struct FDriver
	{
		uint64 Frame = 6000000;
		const FACFCameraEvaluationResult& Step(UACFCameraDirector* Director)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = 1.f / 60.f;
			Context.FrameNumber = ++Frame;
			return Director->Evaluate(Context);
		}
	};

	inline TArray<uint8> Snapshot(const UObject* Object)
	{
		TArray<uint8> Bytes;
		FObjectWriter Writer(const_cast<UObject*>(Object), Bytes);
		return Bytes;
	}

	inline FVector NodeOffset(const UACFCameraPreset* Preset, int32 Index)
	{
		return CastChecked<UACFCameraNode_Follow>(Preset->Nodes[Index].Get())->Offset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPanelWorldTest, "ActionCameraFramework.Editor.TuningPanel.OnlyPIEWorldsAreTargets", ACFTuningPanelTests::Flags)
bool FACFTuningPanelWorldTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningPanelTests;
	TestTrue(TEXT("PIE is a target"), FACFCameraTuningPanelModel::IsTunableWorldType(EWorldType::PIE));
	for (const EWorldType::Type Type : { EWorldType::Editor, EWorldType::EditorPreview, EWorldType::GamePreview, EWorldType::Inactive, EWorldType::Game, EWorldType::None })
	{
		TestFalse(FString::Printf(TEXT("World type %d is not a target"), static_cast<int32>(Type)), FACFCameraTuningPanelModel::IsTunableWorldType(Type));
	}
	bool bEditorWorld = false;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			bEditorWorld |= Context.WorldType == EWorldType::Editor && Context.World() != nullptr;
		}
	}
	TArray<FACFCameraTuningPanelModel::FWorldEntry> Worlds;
	FACFCameraTuningPanelModel::GetPIEWorlds(Worlds);
	TestTrue(TEXT("The editor world exists"), bEditorWorld);
	TestEqual(TEXT("Without PIE: no target world (the editor world is never listed)"), Worlds.Num(), 0);

	FACFCameraTuningPanelModel Model;
	Model.SelectWorld(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr); // even if a tool passed it
	TestNull(TEXT("The editor world does not resolve to a camera"), Model.ResolveDirector());
	TestTrue(TEXT("Validate drops the editor world"), Model.Validate() && Model.GetWorld() == nullptr);
	TestEqual(TEXT("Start without PIE: NoPlayer"), Model.Start(), EResult::NoPlayer);
	TestTrue(TEXT("The message says why"), Model.GetLastMessage().Contains(TEXT("PIE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPanelModelTest, "ActionCameraFramework.Editor.TuningPanel.RequestsNodesValuesCompareStop", ACFTuningPanelTests::Flags)
bool FACFTuningPanelModelTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningPanelTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTwoFollowPreset());
	TStrongObjectPtr<UACFCameraPreset> Other(MakePreset());
	Other->Nodes.Add(NewObject<UACFCameraNode_Follow>(Other.Get()));
	TStrongObjectPtr<USceneComponent> Target(MakeTarget());
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Low = Director->AddRequest(Other.Get(), FollowParams(Target.Get(), 0));
	const FACFCameraRequestHandle High = Director->AddRequest(Preset.Get(), FollowParams(Target.Get(), 5));
	FDriver Driver;
	Driver.Step(Director.Get());
	TArray<TArray<uint8>> Before;
	Before.Add(Snapshot(Preset.Get()));
	for (const UACFCameraNode* Node : Preset->Nodes)
	{
		Before.Add(Snapshot(Node));
	}

	FACFCameraTuningPanelModel Model;
	Model.SetDirectorForTesting(Director.Get());
	TArray<FACFCameraRequestInfo> Requests;
	Model.GetRequests(Requests);
	const FACFCameraRequestInfo* HighInfo = Requests.FindByPredicate([High](const FACFCameraRequestInfo& I) { return I.Handle == High; });
	const FACFCameraRequestInfo* LowInfo = Requests.FindByPredicate([Low](const FACFCameraRequestInfo& I) { return I.Handle == Low; });
	TestTrue(TEXT("Both requests listed with their preset, priority and state"), Requests.Num() == 2 && HighInfo && LowInfo
		&& HighInfo->Preset == Preset->GetFName() && HighInfo->Priority == 5 && HighInfo->bActive && !LowInfo->bActive);

	Model.SelectRequest(High);
	TArray<FACFCameraTuningNodeInfo> Nodes;
	Model.GetNodes(Nodes);
	TestEqual(TEXT("Four nodes"), Nodes.Num(), 4);
	if (Nodes.Num() == 4)
	{
		TestTrue(TEXT("Two Follow nodes told apart by their index"), Nodes[0].NodeClass == Nodes[1].NodeClass && Nodes[0].NodeIndex == 0 && Nodes[1].NodeIndex == 1);
		TestTrue(TEXT("Stage and parameters per node"), Nodes[0].Stage == EACFCameraNodeStage::Body && Nodes[0].Parameters.Contains(EParam::CameraOffset)
			&& Nodes[2].Parameters == TArray<EParam>{ EParam::FieldOfView } && Nodes[3].Stage == EACFCameraNodeStage::PostBlend && Nodes[3].Parameters == TArray<EParam>{ EParam::CollisionRadius });
	}

	Model.SelectNode(1, UACFCameraNode_Follow::StaticClass()->GetFName());
	TestEqual(TEXT("Set before Start: NotTuning"), Model.SetComponent(EParam::CameraOffset, 1, 77.0), EResult::NotTuning);
	TestEqual(TEXT("Start"), Model.Start(), EResult::Ok);
	TestEqual(TEXT("Set Y of the SECOND Follow's offset"), Model.SetComponent(EParam::CameraOffset, 1, 77.0), EResult::Ok);
	const UACFCameraPreset* Copy = Director->GetRequestPreset(High);
	TestTrue(TEXT("Only node 1 changed, only its Y"), NodeOffset(Copy, 1) == FVector(-400.0, 77.0, 100.0) && NodeOffset(Copy, 0) == FVector(-300.0, 0.0, 150.0));
	TArray<FACFCameraTuningPanelModel::FParameterRow> Rows;
	Model.GetParameterRows(Rows);
	const FACFCameraTuningPanelModel::FParameterRow* OffsetRow = Rows.FindByPredicate([](const FACFCameraTuningPanelModel::FParameterRow& R) { return R.Parameter == EParam::CameraOffset; });
	TestTrue(TEXT("Row: original and current"), OffsetRow && OffsetRow->bAvailable && OffsetRow->Value.OriginalValue == FVector(-400.0, 50.0, 100.0) && OffsetRow->Value.Value == FVector(-400.0, 77.0, 100.0));
	TestEqual(TEXT("Rows: the selected node's two parameters, nothing unavailable on this preset"), Rows.Num(), 2);

	Model.SelectNode(2, UACFCameraNode_FixedPose::StaticClass()->GetFName());
	TestEqual(TEXT("Invalid FOV rejected"), Model.SetValue(EParam::FieldOfView, FVector(0.0, 0.0, 0.0)), EResult::InvalidValue);
	TestTrue(TEXT("Message explains the rejection"), Model.GetLastMessage().Contains(TEXT("rejected")));
	TestEqual(TEXT("FOV 60"), Model.SetValue(EParam::FieldOfView, FVector(60.0, 0.0, 0.0)), EResult::Ok);
	TestEqual(TEXT("FOV on screen"), Driver.Step(Director.Get()).Pose.FieldOfView, 60.f);

	// Compare: original values for a moment, then the tuned ones again (no second request, the rig keeps its state).
	const int32 NumRequestsBefore = Director->GetNumRequests();
	TestEqual(TEXT("Show original"), Model.ToggleCompare(), EResult::Ok);
	TestTrue(TEXT("Showing original"), Model.IsShowingOriginal());
	TestEqual(TEXT("Original FOV on screen"), Driver.Step(Director.Get()).Pose.FieldOfView, 80.f);
	TestEqual(TEXT("Editing is blocked while showing the original"), Model.SetValue(EParam::FieldOfView, FVector(70.0, 0.0, 0.0)), EResult::NotTuning);
	TestEqual(TEXT("Show tuned"), Model.ToggleCompare(), EResult::Ok);
	TestEqual(TEXT("Tuned FOV back"), Driver.Step(Director.Get()).Pose.FieldOfView, 60.f);
	TestTrue(TEXT("Tuned offset back"), NodeOffset(Director->GetRequestPreset(High), 1) == FVector(-400.0, 77.0, 100.0));
	TestEqual(TEXT("Compare made no request"), Director->GetNumRequests(), NumRequestsBefore);

	TestEqual(TEXT("Reset FOV"), Model.ResetParameter(EParam::FieldOfView), EResult::Ok);
	TestEqual(TEXT("FOV back to the asset"), Driver.Step(Director.Get()).Pose.FieldOfView, 80.f);
	TestEqual(TEXT("Reset All"), Model.ResetAll(), EResult::Ok);
	TestEqual(TEXT("Nothing differs"), Model.GetStatus().NumChangedValues, 0);
	Model.SelectNode(1, UACFCameraNode_Follow::StaticClass()->GetFName());
	Model.SetComponent(EParam::CameraOffset, 0, -250.0);
	TestEqual(TEXT("Stop"), Model.Stop(), EResult::Ok);
	TestTrue(TEXT("Stopped: the request runs the asset again"), Director->GetRequestPreset(High) == Preset.Get() && !Model.IsTuning());
	bool bUnchanged = Snapshot(Preset.Get()) == Before[0];
	for (int32 I = 0; I < Preset->Nodes.Num(); ++I)
	{
		bUnchanged &= Snapshot(Preset->Nodes[I]) == Before[I + 1];
	}
	TestTrue(TEXT("The asset preset and its nodes are byte-identical"), bUnchanged);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPanelStaleTest, "ActionCameraFramework.Editor.TuningPanel.StaleTargetsAreNotTouched", ACFTuningPanelTests::Flags)
bool FACFTuningPanelStaleTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningPanelTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTwoFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget());
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FDriver Driver;
	Driver.Step(Director.Get());
	FACFCameraTuningPanelModel Model;
	Model.SetDirectorForTesting(Director.Get());
	Model.SelectRequest(Handle);
	Model.SelectNode(0, UACFCameraNode_Follow::StaticClass()->GetFName());
	Model.Start();
	TestFalse(TEXT("Valid target: nothing to drop"), Model.Validate());

	// A node index whose class does not match (e.g. after the request changed) is never written.
	Model.SelectNode(0, UACFCameraNode_Collision::StaticClass()->GetFName());
	TestEqual(TEXT("Mismatching node: NoNode"), Model.SetValue(EParam::CameraOffset, FVector::ZeroVector), EResult::NoNode);
	TestTrue(TEXT("Validate drops the mismatching node"), Model.Validate() && Model.GetNodeIndex() == INDEX_NONE);

	// The request ends: the selection is dropped, operations fail without touching anything.
	Model.SelectNode(0, UACFCameraNode_Follow::StaticClass()->GetFName());
	Director->RemoveRequest(Handle, nullptr, false);
	Driver.Step(Director.Get());
	Driver.Step(Director.Get());
	TestTrue(TEXT("Validate drops the ended request and its node"), Model.Validate() && !Model.GetRequest().IsValid() && Model.GetNodeIndex() == INDEX_NONE);
	TestEqual(TEXT("Set after the request ended: NoRequest"), Model.SetValue(EParam::CameraOffset, FVector::ZeroVector), EResult::NoRequest);
	TestEqual(TEXT("The director has no tuning left (it ended with the request)"), Director->GetNumTunedRequests(), 0);

	// The director goes away (the player / PIE ended): nothing resolves any more.
	Model.SelectRequest(Handle);
	Director.Reset();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNull(TEXT("Destroyed director: nothing resolves"), Model.ResolveDirector());
	TestEqual(TEXT("Start: NoPlayer"), Model.Start(), EResult::NoPlayer);
	TestEqual(TEXT("Set: NoPlayer"), Model.SetValue(EParam::CameraOffset, FVector::ZeroVector), EResult::NoPlayer);
	TestTrue(TEXT("Asset untouched"), NodeOffset(Preset.Get(), 0) == FVector(-300.0, 0.0, 150.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPanelPlayersTest, "ActionCameraFramework.Editor.TuningPanel.PlayersDoNotMix", ACFTuningPanelTests::Flags)
bool FACFTuningPanelPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningPanelTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTwoFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget());
	TStrongObjectPtr<UACFCameraDirector> P1(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> P2(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle H1 = P1->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	const FACFCameraRequestHandle H2 = P2->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FDriver D1;
	FDriver D2;
	D1.Step(P1.Get());
	D2.Step(P2.Get());
	FACFCameraTuningPanelModel M1;
	FACFCameraTuningPanelModel M2;
	M1.SetDirectorForTesting(P1.Get());
	M2.SetDirectorForTesting(P2.Get());
	M1.SelectRequest(H1);
	M1.SelectNode(2, UACFCameraNode_FixedPose::StaticClass()->GetFName());
	M1.Start();
	M1.SetValue(EParam::FieldOfView, FVector(50.0, 0.0, 0.0));
	M2.SelectRequest(H1); // P1's handle on P2's panel
	TestEqual(TEXT("Another player's handle does not resolve"), M2.Start(), EResult::NoRequest);
	M2.SelectRequest(H2);
	TestFalse(TEXT("P2 is not tuning"), M2.IsTuning());
	TestEqual(TEXT("P1 shows its value"), D1.Step(P1.Get()).Pose.FieldOfView, 50.f);
	TestEqual(TEXT("P2 shows the asset value"), D2.Step(P2.Get()).Pose.FieldOfView, 80.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPanelWidgetTest, "ActionCameraFramework.Editor.TuningPanel.WidgetHandlers", ACFTuningPanelTests::Flags)
bool FACFTuningPanelWidgetTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningPanelTests;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Skipped: Slate is not initialized."));
		return true;
	}
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTwoFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget());
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FDriver Driver;
	Driver.Step(Director.Get());

	// The real widget, driven through the handlers its buttons / list / spin boxes call (not by synthesized mouse events).
	TSharedRef<SACFCameraTuningPanel> Panel = SNew(SACFCameraTuningPanel).DirectorForTesting(Director.Get());
	Panel->Refresh();
	TestEqual(TEXT("The request is listed"), Panel->GetNumListedRequests(), 1);
	FACFCameraRequestInfo Info;
	Info.Handle = Handle;
	Panel->OnRequestSelected(MakeShared<FACFCameraRequestInfo>(Info), ESelectInfo::OnMouseClick);
	TestEqual(TEXT("Its four nodes are listed"), Panel->GetNumListedNodes(), 4);
	FACFCameraTuningNodeInfo Node;
	Node.NodeIndex = 2;
	Node.NodeClass = UACFCameraNode_FixedPose::StaticClass()->GetFName();
	Panel->OnNodeSelected(MakeShared<FACFCameraTuningNodeInfo>(Node), ESelectInfo::OnMouseClick);
	TestEqual(TEXT("One parameter row (Field Of View)"), Panel->GetNumParameterRows(), 1);
	Panel->OnStart();
	Panel->OnComponentCommitted(55.0, ETextCommit::OnEnter, EParam::FieldOfView, 0);
	TestEqual(TEXT("Typed value reaches the camera"), Driver.Step(Director.Get()).Pose.FieldOfView, 55.f);
	Panel->OnComponentCommitted(250.0, ETextCommit::OnEnter, EParam::FieldOfView, 0);
	TestEqual(TEXT("Invalid typed value: camera unchanged"), Driver.Step(Director.Get()).Pose.FieldOfView, 55.f);
	TestTrue(TEXT("Invalid typed value: the panel says why"), Panel->GetModel().GetLastMessage().Contains(TEXT("rejected")));
	Panel->OnCompare();
	TestEqual(TEXT("Compare: original on the camera"), Driver.Step(Director.Get()).Pose.FieldOfView, 80.f);
	Panel->OnCompare();
	TestEqual(TEXT("Compare: tuned again"), Driver.Step(Director.Get()).Pose.FieldOfView, 55.f);
	Panel->OnResetParameter(EParam::FieldOfView);
	TestEqual(TEXT("Reset button"), Driver.Step(Director.Get()).Pose.FieldOfView, 80.f);
	Panel->OnComponentCommitted(65.0, ETextCommit::OnEnter, EParam::FieldOfView, 0);
	Panel->OnStop();
	TestEqual(TEXT("Stop button: asset value"), Driver.Step(Director.Get()).Pose.FieldOfView, 80.f);
	TestEqual(TEXT("Asset untouched"), CastChecked<UACFCameraNode_FixedPose>(Preset->Nodes[2].Get())->FieldOfView, 80.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPanelCostTest, "ActionCameraFramework.Editor.TuningPanel.CostOfThePanel", ACFTuningPanelTests::Flags)
bool FACFTuningPanelCostTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningPanelTests;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Skipped: Slate is not initialized."));
		return true;
	}
	// The panel never runs inside a camera evaluation: it polls on its own timer (4 Hz). Here every evaluation is followed by
	// a panel refresh (far more than 4 Hz) to separate the two costs.
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTwoFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget());
	auto Measure = [&](int32 Mode, double& OutEvalUs, double& OutRefreshUs)
	{
		TStrongObjectPtr<UACFCameraDirector> D1(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<UACFCameraDirector> D2(NewObject<UACFCameraDirector>());
		const FACFCameraRequestHandle H1 = D1->AddRequest(Preset.Get(), FollowParams(Target.Get()));
		D2->AddRequest(Preset.Get(), FollowParams(Target.Get()));
		TSharedPtr<SACFCameraTuningPanel> Panel1;
		TSharedPtr<SACFCameraTuningPanel> Panel2;
		if (Mode >= 1)
		{
			Panel1 = SNew(SACFCameraTuningPanel).DirectorForTesting(D1.Get());
			FACFCameraRequestInfo Info;
			Info.Handle = H1;
			Panel1->OnRequestSelected(MakeShared<FACFCameraRequestInfo>(Info), ESelectInfo::OnMouseClick);
			FACFCameraTuningNodeInfo Node;
			Node.NodeIndex = 2;
			Node.NodeClass = UACFCameraNode_FixedPose::StaticClass()->GetFName();
			Panel1->OnNodeSelected(MakeShared<FACFCameraTuningNodeInfo>(Node), ESelectInfo::OnMouseClick);
			Panel1->OnStart();
		}
		if (Mode == 3)
		{
			Panel2 = SNew(SACFCameraTuningPanel).DirectorForTesting(D2.Get());
		}
		FDriver Driver1;
		FDriver Driver2;
		double EvalSeconds = 0.0;
		double RefreshSeconds = 0.0;
		const int32 Frames = 2000;
		for (int32 I = 0; I < Frames; ++I)
		{
			if (Mode == 2)
			{
				Panel1->OnComponentCommitted(60.0 + (I & 7), ETextCommit::OnEnter, EACFCameraTuningParameter::FieldOfView, 0);
			}
			const double T0 = FPlatformTime::Seconds();
			Driver1.Step(D1.Get());
			Driver2.Step(D2.Get());
			const double T1 = FPlatformTime::Seconds();
			if (Panel1) { Panel1->Refresh(); }
			if (Panel2) { Panel2->Refresh(); }
			RefreshSeconds += FPlatformTime::Seconds() - T1;
			EvalSeconds += T1 - T0;
		}
		OutEvalUs = EvalSeconds * 1.0e6 / Frames;
		OutRefreshUs = RefreshSeconds * 1.0e6 / Frames;
	};
	const TCHAR* Labels[4] = { TEXT("panel closed"), TEXT("panel open, no change"), TEXT("panel open, value set every frame"), TEXT("two panels (two players)") };
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		double Eval = 0.0;
		double Refresh = 0.0;
		Measure(Mode, Eval, Refresh);
		AddInfo(FString::Printf(TEXT("%s: camera evaluation of 2 players %.3f us / frame, panel refresh %.3f us / call"), Labels[Mode], Eval, Refresh));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Real PIE (automation drives the editor's own Play In Editor): the panel model finds the PIE world, not the editor
// world, tunes the PIE player's camera on the real PlayerCameraManager, and forgets everything when PIE ends.

namespace ACFTuningPanelTests
{
	struct FPieState
	{
		FAutomationTestBase* Test = nullptr;
		FACFCameraTuningPanelModel Model;
		TStrongObjectPtr<UACFCameraPreset> Preset;
		FACFCameraRequestHandle Handle;
		uint64 MarkFrame = 0;
		int32 Cycle = 0;
	};
	using FPieStep = TFunction<bool(FPieState&)>; // true = step done, false = wait (retry next frame)

	class FPieScenario : public IAutomationLatentCommand
	{
	public:
		FPieScenario(TSharedRef<FPieState> InState, TArray<FPieStep> InSteps) : State(InState), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			if (Index >= Steps.Num())
			{
				return true;
			}
			const double Now = FPlatformTime::Seconds();
			if (StepStart == 0.0)
			{
				StepStart = Now;
			}
			if (Now - StepStart > 90.0)
			{
				State->Test->AddError(FString::Printf(TEXT("PIE scenario step %d timed out."), Index));
				return true;
			}
			if (Steps[Index](*State))
			{
				++Index;
				StepStart = 0.0;
			}
			return Index >= Steps.Num();
		}
	private:
		TSharedRef<FPieState> State;
		TArray<FPieStep> Steps;
		int32 Index = 0;
		double StepStart = 0.0;
	};

	inline APlayerCameraManager* PiePcm(FPieState& S)
	{
		const ULocalPlayer* Player = S.Model.GetPlayer();
		const APlayerController* PC = Player ? Player->GetPlayerController(S.Model.GetWorld()) : nullptr;
		return PC ? PC->PlayerCameraManager.Get() : nullptr;
	}

	inline TArray<FPieStep> MakeTuneSteps()
	{
		TArray<FPieStep> Steps;
		Steps.Add([](FPieState& S)
		{
			TArray<FACFCameraTuningPanelModel::FWorldEntry> Worlds;
			FACFCameraTuningPanelModel::GetPIEWorlds(Worlds);
			TArray<FACFCameraTuningPanelModel::FPlayerEntry> Players;
			FACFCameraTuningPanelModel::GetPlayers(Worlds.Num() > 0 ? Worlds[0].World.Get() : nullptr, Players);
			const APlayerController* PC = Players.Num() > 0 ? Players[0].Player->GetPlayerController(Worlds[0].World.Get()) : nullptr;
			if (!PC || !PC->PlayerCameraManager)
			{
				return false; // PIE still starting
			}
			S.Test->TestEqual(FString::Printf(TEXT("Cycle %d: one PIE world listed"), S.Cycle), Worlds.Num(), 1);
			UWorld* PieWorld = Worlds[0].World.Get();
			S.Test->TestTrue(TEXT("It is the PIE world, not the editor world"), PieWorld->WorldType == EWorldType::PIE && PieWorld != GEditor->GetEditorWorldContext().World());
			S.Model.SelectWorld(PieWorld);
			S.Model.SelectPlayer(Players[0].Player.Get());
			UACFCameraSubsystem* Subsystem = Players[0].Player->GetSubsystem<UACFCameraSubsystem>();
			S.Handle = Subsystem->RequestCamera(S.Preset.Get(), FACFCameraRequestParams());
			S.Test->TestTrue(TEXT("Model resolves the PIE player's camera"), S.Model.ResolveDirector() == Subsystem->GetDirector());
			S.MarkFrame = GFrameCounter;
			return true;
		});
		Steps.Add([](FPieState& S) { return GFrameCounter > S.MarkFrame + 5; });
		Steps.Add([](FPieState& S)
		{
			APlayerCameraManager* PCM = PiePcm(S);
			S.Test->TestTrue(TEXT("Asset FOV on the PIE camera"), PCM && FMath::IsNearlyEqual(PCM->GetFOVAngle(), 70.f, 0.01f));
			S.Model.SelectRequest(S.Handle);
			S.Model.SelectNode(0, UACFCameraNode_FixedPose::StaticClass()->GetFName());
			S.Test->TestEqual(TEXT("Start"), S.Model.Start(), EACFCameraTuningResult::Ok);
			S.Test->TestEqual(TEXT("FOV 50"), S.Model.SetValue(EACFCameraTuningParameter::FieldOfView, FVector(50.0, 0.0, 0.0)), EACFCameraTuningResult::Ok);
			S.MarkFrame = GFrameCounter;
			return true;
		});
		Steps.Add([](FPieState& S) { return GFrameCounter > S.MarkFrame + 5; });
		Steps.Add([](FPieState& S)
		{
			APlayerCameraManager* PCM = PiePcm(S);
			S.Test->TestTrue(FString::Printf(TEXT("Tuned FOV on the PIE camera (%.2f)"), PCM ? PCM->GetFOVAngle() : -1.f), PCM && FMath::IsNearlyEqual(PCM->GetFOVAngle(), 50.f, 0.01f));
			if (S.Cycle == 0)
			{
				return true; // the first cycle ends PIE while tuning: nothing may survive it
			}
			S.Test->TestEqual(TEXT("Stop"), S.Model.Stop(), EACFCameraTuningResult::Ok);
			S.MarkFrame = GFrameCounter;
			return true;
		});
		Steps.Add([](FPieState& S) { return GFrameCounter > S.MarkFrame + 5; });
		Steps.Add([](FPieState& S)
		{
			if (S.Cycle > 0)
			{
				APlayerCameraManager* PCM = PiePcm(S);
				S.Test->TestTrue(TEXT("Stopped: asset FOV on the PIE camera"), PCM && FMath::IsNearlyEqual(PCM->GetFOVAngle(), 70.f, 0.01f));
			}
			return true;
		});
		return Steps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPanelPieTest, "ActionCameraFramework.Editor.TuningPanel.RealPIE", ACFTuningPanelTests::Flags)
bool FACFTuningPanelPieTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningPanelTests;
	TSharedRef<FPieState> State = MakeShared<FPieState>();
	State->Test = this;
	UACFCameraPreset* Preset = MakePreset();
	UACFCameraNode_FixedPose* Fixed = NewObject<UACFCameraNode_FixedPose>(Preset);
	Fixed->Location = FVector(0.0, 0.0, 800.0);
	Fixed->Rotation = FRotator(-30.0, 0.0, 0.0);
	Fixed->FieldOfView = 70.f;
	Preset->Nodes.Add(Fixed);
	State->Preset.Reset(Preset);

	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/ActionCameraFramework/Samples/L_ACFCameraTest")));
	for (int32 Cycle = 0; Cycle < 2; ++Cycle)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		TArray<FPieStep> Steps;
		Steps.Add([Cycle](FPieState& S) { S.Cycle = Cycle; return true; });
		Steps.Append(MakeTuneSteps());
		ADD_LATENT_AUTOMATION_COMMAND(FPieScenario(State, MoveTemp(Steps)));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		TArray<FPieStep> After;
		After.Add([](FPieState& S)
		{
			TArray<FACFCameraTuningPanelModel::FWorldEntry> Worlds;
			FACFCameraTuningPanelModel::GetPIEWorlds(Worlds);
			if (Worlds.Num() > 0 || (GEditor && GEditor->PlayWorld))
			{
				return false; // PIE still shutting down
			}
			S.Test->TestTrue(TEXT("PIE ended: the panel drops the old target"), S.Model.Validate() && !S.Model.GetWorld() && !S.Model.GetPlayer() && !S.Model.GetRequest().IsValid());
			S.Test->TestNull(TEXT("PIE ended: nothing resolves"), S.Model.ResolveDirector());
			S.Test->TestEqual(TEXT("PIE ended: Start is refused"), S.Model.Start(), EACFCameraTuningResult::NoPlayer);
			S.Test->TestEqual(TEXT("PIE ended: the asset FOV is unchanged"), CastChecked<UACFCameraNode_FixedPose>(S.Preset->Nodes[0].Get())->FieldOfView, 70.f);
			return true;
		});
		ADD_LATENT_AUTOMATION_COMMAND(FPieScenario(State, MoveTemp(After)));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
