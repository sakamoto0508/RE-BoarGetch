#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraDiagnosticsModel.h"
#include "ACFCameraPreset.h"
#include "ACFCameraTuningPanelModel.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneSubsystem.h"
#include "ACFCameraZoneValidation.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/DataValidation.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7C-3A: Camera Zone in the editor (README 32 章). Data Validation / Map Check of zones in a temporary editor World, and
 * the sample of L_ACFCameraTest in real PIE with the Debugger's Zone rows.
 */
namespace ACFZoneEditorTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	inline bool Has(const TArray<FACFCameraZoneIssue>& Issues, const TCHAR* Id, EACFCameraPresetIssueSeverity Severity)
	{
		return Issues.ContainsByPredicate([Id, Severity](const FACFCameraZoneIssue& Issue) { return Issue.Id == FName(Id) && Issue.Severity == Severity; });
	}

	inline FString Describe(const TArray<FACFCameraZoneIssue>& Issues)
	{
		TArray<FString> Lines;
		for (const FACFCameraZoneIssue& Issue : Issues)
		{
			Lines.Add(Issue.ToText().ToString());
		}
		return Lines.IsEmpty() ? FString(TEXT("(none)")) : FString::Join(Lines, TEXT(" | "));
	}

	/** A temporary editor World (not the level open in the editor). */
	struct FTempWorld
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UACFCameraPreset> Preset;
		TStrongObjectPtr<UACFCameraPreset> ControlPreset;

		FTempWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("ACFZoneValidationTest"), nullptr, /*bAddToRoot*/ true);
			Preset.Reset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
			ControlPreset.Reset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
			UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(ControlPreset.Get());
			Orbit->InputSource = EACFCameraOrbitInputSource::ControlRotation;
			ControlPreset->Nodes.Add(Orbit);
		}

		~FTempWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}

		AACFCameraZone* Box(const FVector& Center, const FVector& Extent, int32 Priority)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AACFCameraZone* Zone = World->SpawnActor<AACFCameraZone>(Center, FRotator::ZeroRotator, Params);
			Zone->GetBox()->SetBoxExtent(Extent, false);
			Zone->GetZone()->ZonePriority = Priority;
			Zone->GetZone()->Preset = Preset.Get();
			return Zone;
		}

		TArray<FACFCameraZoneIssue> Validate(const UACFCameraZoneComponent* Zone) const
		{
			TArray<FACFCameraZoneIssue> Issues;
			ACFCameraZoneValidation::Validate(*Zone, Issues);
			return Issues;
		}
	};
}

// Each finding with its severity; overlaps the resolver decides are not errors; Data Validation / Map Check report them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneValidationTest, "ActionCameraFramework.Editor.Zone.Validation", ACFZoneEditorTests::Flags)
bool FACFZoneValidationTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneEditorTests;
	using ESeverity = EACFCameraPresetIssueSeverity;
	FTempWorld W;
	if (!TestNotNull(TEXT("Temporary World"), W.World))
	{
		return false;
	}

	AACFCameraZone* Good = W.Box(FVector(0.0, 0.0, 0.0), FVector(300.0, 300.0, 300.0), 0);
	TArray<FACFCameraZoneIssue> Issues = W.Validate(Good->GetZone());
	TestEqual(FString::Printf(TEXT("A plain zone with a preset: no finding (%s)"), *Describe(Issues)), Issues.Num(), 0);

	// The zone alone.
	AACFCameraZone* NoPreset = W.Box(FVector(5000.0, 0.0, 0.0), FVector(300.0, 300.0, 300.0), 0);
	NoPreset->GetZone()->Preset = nullptr;
	TestTrue(TEXT("No preset: warning"), Has(W.Validate(NoPreset->GetZone()), TEXT("ACF.Zone.NoPreset"), ESeverity::Warning));

	AACFCameraZone* Empty = W.Box(FVector(10000.0, 0.0, 0.0), FVector(300.0, 0.0, 300.0), 0);
	TestTrue(TEXT("Box without volume: error"), Has(W.Validate(Empty->GetZone()), TEXT("ACF.Zone.EmptyShape"), ESeverity::Error));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Bare = W.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(15000.0, 0.0, 0.0)), Params);
	UACFCameraZoneComponent* NoShape = NewObject<UACFCameraZoneComponent>(Bare, TEXT("Zone"));
	NoShape->Preset = W.Preset.Get();
	TestTrue(TEXT("No Box / Sphere: error"), Has(W.Validate(NoShape), TEXT("ACF.Zone.NoShape"), ESeverity::Error));

	AActor* Round = W.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(20000.0, 0.0, 0.0), FVector(1.0, 2.0, 1.0)), Params);
	USphereComponent* Sphere = NewObject<USphereComponent>(Round, TEXT("Shape"));
	Sphere->SetSphereRadius(200.f, false);
	Round->SetRootComponent(Sphere);
	Round->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(20000.0, 0.0, 0.0), FVector(1.0, 2.0, 1.0)));
	UACFCameraZoneComponent* Stretched = NewObject<UACFCameraZoneComponent>(Round, TEXT("Zone"));
	Stretched->Preset = W.Preset.Get();
	TestTrue(TEXT("Sphere scaled non-uniformly: warning"), Has(W.Validate(Stretched), TEXT("ACF.Zone.SphereScale"), ESeverity::Warning));

	AACFCameraZone* Timing = W.Box(FVector(25000.0, 0.0, 0.0), FVector(300.0, 300.0, 300.0), 0);
	UACFCameraZoneComponent* T = Timing->GetZone();
	T->bOverrideBlendIn = true;
	T->BlendIn.Curve = EACFCameraBlendCurve::Linear;
	T->BlendIn.Duration = NAN;
	T->bOverrideBlendOut = true;
	T->BlendOut.Curve = EACFCameraBlendCurve::Custom;
	T->BlendOut.CustomCurve = nullptr;
	T->EnterDelay = -1.f;
	T->ExitDelay = 20.f;
	T->ExitMargin = NAN;
	Issues = W.Validate(T);
	TestTrue(FString::Printf(TEXT("Blend In NaN: error (%s)"), *Describe(Issues)), Has(Issues, TEXT("ACF.Zone.Blend.NonFinite"), ESeverity::Error));
	TestTrue(TEXT("Custom curve without asset: warning"), Has(Issues, TEXT("ACF.Zone.Blend.MissingCurve"), ESeverity::Warning));
	TestTrue(TEXT("Negative Enter Delay: error"), Has(Issues, TEXT("ACF.Zone.Delay.Invalid"), ESeverity::Error));
	TestTrue(TEXT("20 s Exit Delay: warning"), Has(Issues, TEXT("ACF.Zone.Delay.Long"), ESeverity::Warning));
	TestTrue(TEXT("Exit Margin NaN: error"), Has(Issues, TEXT("ACF.Zone.Margin.Invalid"), ESeverity::Error));
	T->BlendIn.Duration = -0.5f;
	TestTrue(TEXT("Negative blend: warning (a cut)"), Has(W.Validate(T), TEXT("ACF.Zone.Blend.Negative"), ESeverity::Warning));

	AACFCameraZone* Keep = W.Box(FVector(30000.0, 0.0, 0.0), FVector(300.0, 300.0, 300.0), 0);
	Keep->GetZone()->Preset = W.ControlPreset.Get();
	Keep->GetZone()->ExitRotation = EACFCameraZoneRotationHandoff::KeepView;
	Keep->GetZone()->SetZoneEnabled(false);
	Issues = W.Validate(Keep->GetZone());
	TestTrue(TEXT("Keep View with a Control Rotation camera: info"), Has(Issues, TEXT("ACF.Zone.KeepViewControlRotation"), ESeverity::Info));
	TestTrue(TEXT("Starts disabled: info"), Has(Issues, TEXT("ACF.Zone.Disabled"), ESeverity::Info));
	TestTrue(TEXT("Only infos: the zone is fine"), ACFCameraZoneValidation::MaxSeverity(Issues) == ESeverity::Info);

	// Overlaps: decided by priority or volume = no warning; an exact tie = warning on both; different priorities = nothing.
	AACFCameraZone* TieA = W.Box(FVector(0.0, 5000.0, 0.0), FVector(300.0, 300.0, 300.0), 2);
	AACFCameraZone* TieB = W.Box(FVector(100.0, 5000.0, 0.0), FVector(300.0, 300.0, 300.0), 2);
	AACFCameraZone* Inner = W.Box(FVector(0.0, 5000.0, 0.0), FVector(100.0, 100.0, 100.0), 2);
	AACFCameraZone* Higher = W.Box(FVector(0.0, 5000.0, 0.0), FVector(50.0, 50.0, 50.0), 3);
	Issues = W.Validate(TieA->GetZone());
	TestTrue(FString::Printf(TEXT("Same priority and volume, overlapping: warning (%s)"), *Describe(Issues)), Has(Issues, TEXT("ACF.Zone.Tie"), ESeverity::Warning));
	TestTrue(TEXT("... reported on the other one too"), Has(W.Validate(TieB->GetZone()), TEXT("ACF.Zone.Tie"), ESeverity::Warning));
	Issues = W.Validate(Inner->GetZone());
	TestTrue(TEXT("Same priority, smaller inside: info only (the resolver decides)"), Has(Issues, TEXT("ACF.Zone.Overlap"), ESeverity::Info) && ACFCameraZoneValidation::MaxSeverity(Issues) == ESeverity::Info);
	TestEqual(FString::Printf(TEXT("Higher priority inside: nothing (%s)"), *Describe(W.Validate(Higher->GetZone()))), W.Validate(Higher->GetZone()).Num(), 0);
	TestFalse(TEXT("A zone far away overlaps nothing"), Has(W.Validate(Good->GetZone()), TEXT("ACF.Zone.Tie"), ESeverity::Warning));

	// Data Validation through the component (and so through the actor) and Map Check.
	{
		FDataValidationContext Context;
		TestEqual(TEXT("Data Validation: an error makes the zone invalid"), Empty->GetZone()->IsDataValid(Context), EDataValidationResult::Invalid);
		TestTrue(TEXT("... with the message"), Context.GetNumErrors() > 0);
	}
	{
		FDataValidationContext Context;
		TestEqual(TEXT("Data Validation: warnings only are valid"), NoPreset->IsDataValid(Context), EDataValidationResult::Valid);
		TestTrue(TEXT("... the actor reports its zone's warning"), Context.GetNumWarnings() > 0);
	}
	// Map Check: the error reaches the MapCheck log (expected here; at least once).
	AddExpectedMessagePlain(TEXT("[ACF.Zone.EmptyShape]"), EAutomationExpectedMessageFlags::Contains, 0);
	Empty->CheckForErrors();
	return true;
}

// -------------------------------------------------------------------------------------------------
// The sample of L_ACFCameraTest in real PIE: the sample zones pass validation in the editor World; in PIE, ACF.Sample.Zone
// puts the pawn into a zone and the Debugger's Zone rows show the selection, the pending change with its time left, the zone
// request and the camera in control.

namespace ACFZoneEditorTests
{
	struct FPieState
	{
		FAutomationTestBase* Test = nullptr;
		FACFCameraTuningPanelModel Model;
		TUniquePtr<FACFCameraDiagnosticsModel> Diagnostics;
		uint64 Mark = 0;
		FVector Corridor = FVector::ZeroVector;
	};
	using FPieStep = TFunction<bool(FPieState&)>;

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
			StepStart = StepStart == 0.0 ? Now : StepStart;
			if (Now - StepStart > 60.0)
			{
				State->Test->AddError(FString::Printf(TEXT("Zone PIE scenario step %d timed out."), Index));
				Index = Steps.Num();
				State->Diagnostics.Reset();
				return true;
			}
			if (Steps[Index](*State))
			{
				++Index;
				StepStart = 0.0;
				State->Mark = GFrameCounter;
			}
			if (Index >= Steps.Num())
			{
				State->Diagnostics.Reset();
				return true;
			}
			return false;
		}
	private:
		TSharedRef<FPieState> State;
		TArray<FPieStep> Steps;
		int32 Index = 0;
		double StepStart = 0.0;
	};

	inline FString Row(const FACFCameraDiagnosticsModel::FDebugView& View, const TCHAR* Label)
	{
		const FACFCameraDiagnosticsModel::FRow* Found = FACFCameraDiagnosticsModel::FindRow(View, Label);
		return Found ? Found->Value : FString(TEXT("<missing>"));
	}

	inline APawn* PiePawn(FPieState& S)
	{
		const ULocalPlayer* Player = S.Model.GetPlayer();
		const APlayerController* PC = Player ? Player->GetPlayerController(S.Model.GetWorld()) : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneSamplePieTest, "ActionCameraFramework.Editor.Zone.SampleRealPIE", ACFZoneEditorTests::Flags)
bool FACFZoneSamplePieTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneEditorTests;
	TSharedRef<FPieState> State = MakeShared<FPieState>();
	State->Test = this;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/ActionCameraFramework/Samples/L_ACFCameraTest")));
	TArray<FPieStep> Before;
	Before.Add([](FPieState& S)
	{
		// The sample zones in the editor World: no error / warning (overlap infos are expected: the resolver decides them).
		UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		int32 Zones = 0;
		for (TActorIterator<AActor> It(EditorWorld); It; ++It)
		{
			if (!It->ActorHasTag(TEXT("ACFSampleZone")))
			{
				continue;
			}
			++Zones;
			UACFCameraZoneComponent* Zone = It->FindComponentByClass<UACFCameraZoneComponent>();
			TArray<FACFCameraZoneIssue> Issues;
			ACFCameraZoneValidation::Validate(*Zone, Issues);
			S.Test->TestTrue(FString::Printf(TEXT("Sample zone %s: no error / warning (%s)"), *It->GetActorNameOrLabel(), *Describe(Issues)), ACFCameraZoneValidation::MaxSeverity(Issues) == EACFCameraPresetIssueSeverity::Info);
			if (It->ActorHasTag(TEXT("ACFSampleZone_Corridor")))
			{
				S.Corridor = It->GetActorLocation();
			}
		}
		S.Test->TestEqual(TEXT("Three sample zones in L_ACFCameraTest (Scripts/CreateZoneSampleAssets.py)"), Zones, 3);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FPieScenario(State, MoveTemp(Before)));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	TArray<FPieStep> Steps;
	Steps.Add([](FPieState& S)
	{
		TArray<FACFCameraTuningPanelModel::FWorldEntry> Worlds;
		FACFCameraTuningPanelModel::GetPIEWorlds(Worlds);
		TArray<FACFCameraTuningPanelModel::FPlayerEntry> Players;
		FACFCameraTuningPanelModel::GetPlayers(Worlds.Num() > 0 ? Worlds[0].World.Get() : nullptr, Players);
		if (Players.IsEmpty())
		{
			return false; // PIE starting
		}
		S.Model.SelectWorld(Worlds[0].World.Get());
		S.Model.SelectPlayer(Players[0].Player.Get());
		if (!PiePawn(S))
		{
			return false;
		}
		S.Diagnostics = MakeUnique<FACFCameraDiagnosticsModel>(S.Model);
		GEngine->Exec(S.Model.GetWorld(), TEXT("ACF.Sample.Zone Go Corridor"));
		return true;
	});
	Steps.Add([](FPieState& S) { return GFrameCounter > S.Mark + 5; });
	Steps.Add([](FPieState& S)
	{
		const FACFCameraDiagnosticsModel::FDebugView View = S.Diagnostics->ReadDebug();
		S.Test->TestTrue(TEXT("Debugger has a view"), View.bValid);
		S.Test->TestTrue(FString::Printf(TEXT("Selected zone row: %s"), *Row(View, TEXT("Selected zone"))), Row(View, TEXT("Selected zone")).Contains(TEXT("Zone_Corridor")));
		S.Test->TestTrue(FString::Printf(TEXT("Zone request row: %s"), *Row(View, TEXT("Zone request"))), Row(View, TEXT("Zone request")).Contains(TEXT("in control")));
		S.Test->TestTrue(FString::Printf(TEXT("Camera in control row: %s"), *Row(View, TEXT("Camera in control"))), Row(View, TEXT("Camera in control")).Contains(TEXT("= the zone request")));
		S.Test->TestEqual(TEXT("No pending change"), Row(View, TEXT("Pending candidate")), FString(TEXT("none")));
		// Long delays, then a walk of 400 cm out of the corridor into the plaza (not a teleport): the change waits.
		GEngine->Exec(S.Model.GetWorld(), TEXT("ACF.Sample.Zone Delay 5 5"));
		PiePawn(S)->SetActorLocation(PiePawn(S)->GetActorLocation() + FVector(0.0, 400.0, 0.0));
		return true;
	});
	Steps.Add([](FPieState& S) { return GFrameCounter > S.Mark + 5; });
	Steps.Add([](FPieState& S)
	{
		const FACFCameraDiagnosticsModel::FDebugView View = S.Diagnostics->ReadDebug();
		const FString Pending = Row(View, TEXT("Pending candidate"));
		S.Test->TestTrue(FString::Printf(TEXT("Pending candidate row: %s"), *Pending), Pending.Contains(TEXT("Zone_Plaza")) && Pending.Contains(TEXT("/ 5.00 s")) && Pending.Contains(TEXT("left")));
		S.Test->TestTrue(TEXT("Still the corridor while waiting"), Row(View, TEXT("Selected zone")).Contains(TEXT("Zone_Corridor")));
		// A Lock-On above the zone: the zone request is below it, the camera in control is not the zone request.
		GEngine->Exec(S.Model.GetWorld(), TEXT("ACF.Sample.LockOn 7"));
		return true;
	});
	Steps.Add([](FPieState& S) { return GFrameCounter > S.Mark + 5; });
	Steps.Add([](FPieState& S)
	{
		const FACFCameraDiagnosticsModel::FDebugView View = S.Diagnostics->ReadDebug();
		S.Test->TestTrue(FString::Printf(TEXT("Zone request below the Lock-On: %s"), *Row(View, TEXT("Zone request"))), Row(View, TEXT("Zone request")).Contains(TEXT("below")));
		S.Test->TestTrue(FString::Printf(TEXT("Camera in control is not the zone request: %s"), *Row(View, TEXT("Camera in control"))), Row(View, TEXT("Camera in control")).Contains(TEXT("not the zone request")));
		GEngine->Exec(S.Model.GetWorld(), TEXT("ACF.Sample.LockOff 0.3"));
		GEngine->Exec(S.Model.GetWorld(), TEXT("ACF.Sample.Zone Delay 0 0"));
		return true;
	});
	Steps.Add([](FPieState& S) { return GFrameCounter > S.Mark + 60; });
	Steps.Add([](FPieState& S)
	{
		const FACFCameraDiagnosticsModel::FDebugView View = S.Diagnostics->ReadDebug();
		S.Test->TestTrue(FString::Printf(TEXT("After the Lock-On: the plaza zone in control (%s / %s)"), *Row(View, TEXT("Selected zone")), *Row(View, TEXT("Camera in control"))),
			Row(View, TEXT("Selected zone")).Contains(TEXT("Zone_Plaza")) && Row(View, TEXT("Camera in control")).Contains(TEXT("= the zone request")));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FPieScenario(State, MoveTemp(Steps)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
