#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "ACFCameraTestUtils.h"
#include "ACFCameraPresetValidation.h"
#include "ACFCameraSequence.h"
#include "Components/SceneComponent.h"
#include "Misc/DataValidation.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_Combat.h"
#include "Nodes/ACFCameraNode_Composers.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Nodes/ACFCameraNode_ShotAnchor.h"
#include "Algo/Count.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"

/** Phase 6B / B-8: static preset validation (ACFCameraPresetValidation, UACFCameraPreset::IsDataValid). */
namespace ACFPresetValidationTests
{
	using namespace ACFTests;
	using ESeverity = EACFCameraPresetIssueSeverity;

	inline UACFCameraPreset* MakePreset()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = Linear(0.5f);
		Preset->DefaultBlendOut = Linear(0.5f);
		return Preset;
	}

	template <typename T>
	T* AddNode(UACFCameraPreset* Preset)
	{
		T* Node = NewObject<T>(Preset);
		Preset->Nodes.Add(Node);
		return Node;
	}

	inline TArray<FACFCameraPresetIssue> Validate(const UACFCameraPreset* Preset)
	{
		TArray<FACFCameraPresetIssue> Issues;
		ACFCameraPresetValidation::ValidatePreset(*Preset, Issues);
		return Issues;
	}

	inline int32 Count(const TArray<FACFCameraPresetIssue>& Issues, ESeverity Severity)
	{
		return static_cast<int32>(Algo::CountIf(Issues, [Severity](const FACFCameraPresetIssue& Issue) { return Issue.Severity == Severity; }));
	}

	inline int32 CountId(const TArray<FACFCameraPresetIssue>& Issues, const TCHAR* Id)
	{
		return static_cast<int32>(Algo::CountIf(Issues, [Id](const FACFCameraPresetIssue& Issue) { return Issue.Id == FName(Id); }));
	}

	/** First issue with Id (and Property, if given). */
	inline const FACFCameraPresetIssue* Find(const TArray<FACFCameraPresetIssue>& Issues, const TCHAR* Id, const TCHAR* Property = nullptr)
	{
		return Issues.FindByPredicate([Id, Property](const FACFCameraPresetIssue& Issue)
		{
			return Issue.Id == FName(Id) && (!Property || Issue.Property == Property);
		});
	}

	inline FString Describe(const TArray<FACFCameraPresetIssue>& Issues)
	{
		FString Text;
		for (const FACFCameraPresetIssue& Issue : Issues)
		{
			Text += Issue.ToText().ToString() + TEXT("\n");
		}
		return Text;
	}

	inline USceneComponent* MakeTarget(const FVector& Location)
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetWorldLocation(Location);
		return Component;
	}

	inline TArray<uint8> Snapshot(const UObject* Object)
	{
		TArray<uint8> Bytes;
		FObjectWriter Writer(const_cast<UObject*>(Object), Bytes);
		return Bytes;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationValidTest, "ActionCameraFramework.PresetValidation.ValidPresetsPass", ACFTests::Flags)
bool FACFPresetValidationValidTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	auto ExpectClean = [this](const UACFCameraPreset* Preset, const TCHAR* What)
	{
		const TArray<FACFCameraPresetIssue> Issues = Validate(Preset);
		TestEqual(FString::Printf(TEXT("%s: no findings\n%s"), What, *Describe(Issues)), Issues.Num(), 0);
		FDataValidationContext Context;
		TestEqual(FString::Printf(TEXT("%s: Data Validation says Valid"), What), Preset->IsDataValid(Context), EDataValidationResult::Valid);
		TestEqual(FString::Printf(TEXT("%s: no warnings"), What), static_cast<int32>(Context.GetNumWarnings()), 0);
		TestEqual(FString::Printf(TEXT("%s: no errors"), What), static_cast<int32>(Context.GetNumErrors()), 0);
	};

	TStrongObjectPtr<UACFCameraPreset> Orbit(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Orbit.Get());
	AddNode<UACFCameraNode_Collision>(Orbit.Get());
	ExpectClean(Orbit.Get(), TEXT("Orbital Follow + Collision"));

	// Rotation Composer refines the orbit rotation (it starts from the incoming rotation): not an override.
	TStrongObjectPtr<UACFCameraPreset> Composed(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Composed.Get());
	AddNode<UACFCameraNode_RotationComposer>(Composed.Get());
	AddNode<UACFCameraNode_Collision>(Composed.Get());
	ExpectClean(Composed.Get(), TEXT("Orbital Follow + Rotation Composer + Collision"));

	TStrongObjectPtr<UACFCameraPreset> FollowLook(MakePreset());
	AddNode<UACFCameraNode_Follow>(FollowLook.Get());
	AddNode<UACFCameraNode_LookAt>(FollowLook.Get());
	AddNode<UACFCameraNode_Collision>(FollowLook.Get());
	ExpectClean(FollowLook.Get(), TEXT("Follow + Look At + Collision"));

	TStrongObjectPtr<UACFCameraPreset> LockOn(MakePreset());
	AddNode<UACFCameraNode_LockOn>(LockOn.Get());
	AddNode<UACFCameraNode_Combat>(LockOn.Get());
	AddNode<UACFCameraNode_GroupFraming>(LockOn.Get());
	AddNode<UACFCameraNode_Collision>(LockOn.Get());
	ExpectClean(LockOn.Get(), TEXT("Lock-On + Combat + Group Framing + Collision"));

	TStrongObjectPtr<UACFCameraPreset> Shot(MakePreset());
	AddNode<UACFCameraNode_ShotAnchor>(Shot.Get());
	AddNode<UACFCameraNode_Collision>(Shot.Get());
	ExpectClean(Shot.Get(), TEXT("Shot Anchor + Collision"));

	TStrongObjectPtr<UACFCameraPreset> Fixed(MakeFixedPreset(FVector(0, 0, 300), 70.f));
	ExpectClean(Fixed.Get(), TEXT("Fixed Pose (cut blends)"));

	// Intentional splits of one role are not conflicts: FOV-only Fixed Pose on top of an orbit, location / FOV from two Fixed Poses.
	TStrongObjectPtr<UACFCameraPreset> Zoom(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Zoom.Get());
	UACFCameraNode_FixedPose* ZoomFov = AddNode<UACFCameraNode_FixedPose>(Zoom.Get());
	ZoomFov->bOverrideLocation = false;
	ZoomFov->bOverrideRotation = false;
	ZoomFov->FieldOfView = 50.f;
	AddNode<UACFCameraNode_Collision>(Zoom.Get());
	ExpectClean(Zoom.Get(), TEXT("Orbital Follow + FOV-only Fixed Pose"));

	TStrongObjectPtr<UACFCameraPreset> Split(MakePreset());
	UACFCameraNode_FixedPose* Where = AddNode<UACFCameraNode_FixedPose>(Split.Get());
	Where->bOverrideFieldOfView = false;
	UACFCameraNode_FixedPose* Lens = AddNode<UACFCameraNode_FixedPose>(Split.Get());
	Lens->bOverrideLocation = false;
	Lens->bOverrideRotation = false;
	ExpectClean(Split.Get(), TEXT("Fixed Pose location/rotation + Fixed Pose FOV"));

	// A disabled duplicate (A/B tuning) is not a conflict either.
	TStrongObjectPtr<UACFCameraPreset> Tuning(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Tuning.Get());
	AddNode<UACFCameraNode_OrbitalFollow>(Tuning.Get())->bEnabled = false;
	AddNode<UACFCameraNode_Collision>(Tuning.Get());
	ExpectClean(Tuning.Get(), TEXT("Disabled duplicate Orbital Follow"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationBodyConflictTest, "ActionCameraFramework.PresetValidation.FixedPoseAndShotAnchorConflict", ACFTests::Flags)
bool FACFPresetValidationBodyConflictTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	AddNode<UACFCameraNode_ShotAnchor>(Preset.Get());
	UACFCameraNode_FixedPose* Fixed = AddNode<UACFCameraNode_FixedPose>(Preset.Get());
	AddNode<UACFCameraNode_Collision>(Preset.Get());
	TArray<FACFCameraPresetIssue> Issues = Validate(Preset.Get());
	AddInfo(Describe(Issues));
	TestEqual(TEXT("Location, rotation and FOV are each set by both nodes"), CountId(Issues, TEXT("ACF.Preset.BodyConflict")), 3);
	const FACFCameraPresetIssue* Conflict = Find(Issues, TEXT("ACF.Preset.BodyConflict"));
	TestTrue(TEXT("Conflict is a warning on the node that wins (Fixed Pose, last in the list)"),
		Conflict && Conflict->Severity == ESeverity::Warning && Conflict->Node == Fixed && Conflict->NodeIndex == 1 && Conflict->Preset == Preset.Get());
	TestFalse(TEXT("The message has a fix"), Conflict && Conflict->Fix.IsEmpty());

	// Location only: Fixed Pose for rotation / FOV after the Shot Anchor still competes for location.
	Fixed->bOverrideRotation = false;
	Fixed->bOverrideFieldOfView = false;
	Issues = Validate(Preset.Get());
	TestEqual(TEXT("Only location left"), CountId(Issues, TEXT("ACF.Preset.BodyConflict")), 1);
	Fixed->bOverrideLocation = false;
	Issues = Validate(Preset.Get());
	TestEqual(TEXT("Nothing left to conflict"), CountId(Issues, TEXT("ACF.Preset.BodyConflict")), 0);

	// The existing role check (Follow + Position Composer + Look At) keeps its two warnings, now with ids.
	TStrongObjectPtr<UACFCameraPreset> Legacy(MakePreset());
	AddNode<UACFCameraNode_Follow>(Legacy.Get());
	AddNode<UACFCameraNode_PositionComposer>(Legacy.Get());
	AddNode<UACFCameraNode_LookAt>(Legacy.Get());
	Issues = Validate(Legacy.Get());
	TestEqual(TEXT("Follow + Position Composer: location conflict"), CountId(Issues, TEXT("ACF.Preset.BodyConflict")), 1);
	TestEqual(TEXT("Position Composer + Aim"), CountId(Issues, TEXT("ACF.Preset.ComposerAim")), 1);
	TestEqual(TEXT("Two warnings in total"), Count(Issues, ESeverity::Warning), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationStageTest, "ActionCameraFramework.PresetValidation.SameStageProblems", ACFTests::Flags)
bool FACFPresetValidationStageTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	// Look At listed first: it still runs after the Body nodes (stage order), noted as Info.
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	AddNode<UACFCameraNode_LookAt>(Preset.Get());
	AddNode<UACFCameraNode_Follow>(Preset.Get());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	UACFCameraNode_RotationComposer* Composer = AddNode<UACFCameraNode_RotationComposer>(Preset.Get());
	AddNode<UACFCameraNode_Collision>(Preset.Get());
	AddNode<UACFCameraNode_Collision>(Preset.Get());
	AddNode<UACFCameraNode_Combat>(Preset.Get());
	AddNode<UACFCameraNode_Combat>(Preset.Get());
	const TArray<FACFCameraPresetIssue> Issues = Validate(Preset.Get());
	AddInfo(Describe(Issues));

	const FACFCameraPresetIssue* Location = Find(Issues, TEXT("ACF.Preset.BodyConflict"));
	TestTrue(TEXT("Follow + Orbital Follow: location conflict, the orbit (later in the Body stage) wins"), Location && Location->Node == Orbit);
	const FACFCameraPresetIssue* Aim = Find(Issues, TEXT("ACF.Preset.AimConflict"));
	TestTrue(TEXT("Look At + Rotation Composer: aim conflict, the composer (later in the Aim stage) wins"), Aim && Aim->Node == Composer && Aim->Severity == ESeverity::Warning);
	TestEqual(TEXT("Orbit rotation replaced by the Aim stage (info)"), CountId(Issues, TEXT("ACF.Preset.RotationOverriddenByAim")), 1);
	const FACFCameraPresetIssue* Order = Find(Issues, TEXT("ACF.Preset.EvaluationOrder"));
	TestTrue(TEXT("List order differs from the evaluation order (info)"), Order && Order->Severity == ESeverity::Info);
	TestEqual(TEXT("Two Collision nodes"), CountId(Issues, TEXT("ACF.Preset.DuplicateCollision")), 1);
	TestEqual(TEXT("Two Combat nodes"), CountId(Issues, TEXT("ACF.Preset.DuplicateCombat")), 1);
	TestEqual(TEXT("No errors: everything still runs"), Count(Issues, ESeverity::Error), 0);

	// Same stack in evaluation order: no order note.
	TStrongObjectPtr<UACFCameraPreset> Ordered(MakePreset());
	AddNode<UACFCameraNode_Follow>(Ordered.Get());
	AddNode<UACFCameraNode_LookAt>(Ordered.Get());
	AddNode<UACFCameraNode_Combat>(Ordered.Get());
	AddNode<UACFCameraNode_GroupFraming>(Ordered.Get());
	AddNode<UACFCameraNode_Collision>(Ordered.Get());
	TestEqual(TEXT("Ordered list: no evaluation order note"), CountId(Validate(Ordered.Get()), TEXT("ACF.Preset.EvaluationOrder")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationNullTest, "ActionCameraFramework.PresetValidation.NullNodesAndReferences", ACFTests::Flags)
bool FACFPresetValidationNullTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	Preset->Nodes.Add(nullptr);
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Preset->Nodes.Add(nullptr);
	Preset->Nodes.Add(Orbit); // same object twice
	TArray<FACFCameraPresetIssue> Issues = Validate(Preset.Get());
	AddInfo(Describe(Issues));
	TestEqual(TEXT("Two empty slots"), CountId(Issues, TEXT("ACF.Preset.EmptyNodeSlot")), 2);
	TestTrue(TEXT("Empty slot reports its index"), Issues.ContainsByPredicate([](const FACFCameraPresetIssue& Issue) { return Issue.Id == TEXT("ACF.Preset.EmptyNodeSlot") && Issue.NodeIndex == 2 && !Issue.Node; }));
	const FACFCameraPresetIssue* Duplicate = Find(Issues, TEXT("ACF.Preset.DuplicateNodeReference"));
	TestTrue(TEXT("Same node object in two slots"), Duplicate && Duplicate->NodeIndex == 3 && Duplicate->Node == Orbit);
	TestEqual(TEXT("The shared node is checked once (no self conflict)"), CountId(Issues, TEXT("ACF.Preset.BodyConflict")), 0);
	TestFalse(TEXT("ToText of an empty slot works"), Issues[0].ToText().IsEmpty());

	TStrongObjectPtr<UACFCameraPreset> Empty(MakePreset());
	Issues = Validate(Empty.Get());
	TestTrue(TEXT("No nodes"), Issues.Num() == 1 && Issues[0].Id == TEXT("ACF.Preset.NoNodes"));

	TArray<FACFCameraPresetIssue> Transition;
	ACFCameraPresetValidation::ValidateTransition(nullptr, Preset.Get(), Transition);
	ACFCameraPresetValidation::ValidateTransition(Preset.Get(), nullptr, Transition);
	ACFCameraPresetValidation::ValidateTransition(Preset.Get(), Preset.Get(), Transition);
	TestEqual(TEXT("Null / same-preset transitions report nothing"), Transition.Num(), 0);
	TestFalse(TEXT("Null preset has no collision"), ACFCameraPresetValidation::HasCollision(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationNonFiniteTest, "ActionCameraFramework.PresetValidation.NonFiniteParameters", ACFTests::Flags)
bool FACFPresetValidationNonFiniteTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_FixedPose* Fixed = AddNode<UACFCameraNode_FixedPose>(Preset.Get());
	Fixed->Location.X = std::numeric_limits<double>::quiet_NaN();
	UACFCameraNode_Follow* Follow = AddNode<UACFCameraNode_Follow>(Preset.Get());
	Follow->bEnabled = false;
	Follow->TargetMotion.TeleportDistance = std::numeric_limits<float>::infinity();
	Preset->DefaultBlendIn.Duration = std::numeric_limits<float>::infinity();
	TArray<FACFCameraPresetIssue> Issues = Validate(Preset.Get());
	AddInfo(Describe(Issues));

	const FACFCameraPresetIssue* Location = Find(Issues, TEXT("ACF.Param.NonFinite"), TEXT("Location.X"));
	TestTrue(TEXT("NaN location component is an error on its node"), Location && Location->Severity == ESeverity::Error && Location->Node == Fixed && Location->NodeIndex == 0);
	const FACFCameraPresetIssue* Blend = Find(Issues, TEXT("ACF.Param.NonFinite"), TEXT("DefaultBlendIn.Duration"));
	TestTrue(TEXT("Infinite blend duration is a preset-level error"), Blend && Blend->Severity == ESeverity::Error && Blend->NodeIndex == INDEX_NONE);
	const FACFCameraPresetIssue* Disabled = Find(Issues, TEXT("ACF.Param.NonFinite"), TEXT("TargetMotion.TeleportDistance"));
	TestTrue(TEXT("Value of a disabled node is a warning (breaks once enabled)"), Disabled && Disabled->Severity == ESeverity::Warning && Disabled->Node == Follow);

	FDataValidationContext Context;
	TestEqual(TEXT("Data Validation result is Invalid"), Preset->IsDataValid(Context), EDataValidationResult::Invalid);
	TestEqual(TEXT("Two errors reach Data Validation"), static_cast<int32>(Context.GetNumErrors()), 2);

	// Unused option (Override Field Of View off): only a warning, and no FOV range error.
	TStrongObjectPtr<UACFCameraPreset> Unused(MakePreset());
	UACFCameraNode_FixedPose* NoFov = AddNode<UACFCameraNode_FixedPose>(Unused.Get());
	NoFov->bOverrideFieldOfView = false;
	NoFov->FieldOfView = std::numeric_limits<float>::quiet_NaN();
	Issues = Validate(Unused.Get());
	const FACFCameraPresetIssue* UnusedFov = Find(Issues, TEXT("ACF.Param.NonFinite"), TEXT("FieldOfView"));
	TestTrue(TEXT("NaN behind an off toggle is a warning"), UnusedFov && UnusedFov->Severity == ESeverity::Warning);
	TestEqual(TEXT("No errors"), Count(Issues, ESeverity::Error), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationNegativeTest, "ActionCameraFramework.PresetValidation.NegativeDampingAndRadius", ACFTests::Flags)
bool FACFPresetValidationNegativeTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_Follow* Follow = AddNode<UACFCameraNode_Follow>(Preset.Get());
	Follow->Damping.Time = FVector(-0.2, 0.1, 0.1);
	UACFCameraNode_LookAt* Look = AddNode<UACFCameraNode_LookAt>(Preset.Get());
	Look->DampingTime = -1.f;
	UACFCameraNode_Collision* Collision = AddNode<UACFCameraNode_Collision>(Preset.Get());
	Collision->ProbeRadius = -5.f;
	Preset->DefaultBlendOut.Duration = -1.f;
	const TArray<FACFCameraPresetIssue> Issues = Validate(Preset.Get());
	AddInfo(Describe(Issues));

	const FACFCameraPresetIssue* Damping = Find(Issues, TEXT("ACF.Param.OutOfRange"), TEXT("Damping.Time.X"));
	TestTrue(TEXT("Negative per-axis damping is a warning on the axis"), Damping && Damping->Severity == ESeverity::Warning && Damping->Node == Follow);
	TestTrue(TEXT("The message says how it behaves"), Damping && Damping->Message.ToString().Contains(TEXT("behaves as 0")));
	TestNull(TEXT("Valid axes are not reported"), Find(Issues, TEXT("ACF.Param.OutOfRange"), TEXT("Damping.Time.Y")));
	TestNotNull(TEXT("Negative Look At damping"), Find(Issues, TEXT("ACF.Param.OutOfRange"), TEXT("DampingTime")));
	const FACFCameraPresetIssue* Radius = Find(Issues, TEXT("ACF.Param.OutOfRange"), TEXT("ProbeRadius"));
	TestTrue(TEXT("Negative collision radius (UE falls back to a line trace)"), Radius && Radius->Node == Collision && Radius->Message.ToString().Contains(TEXT("line trace")));
	const FACFCameraPresetIssue* Blend = Find(Issues, TEXT("ACF.Param.OutOfRange"), TEXT("DefaultBlendOut.Duration"));
	TestTrue(TEXT("Negative blend duration (a cut)"), Blend && Blend->Message.ToString().Contains(TEXT("cut")));
	TestEqual(TEXT("These still run: no errors"), Count(Issues, ESeverity::Error), 0);

	// Feature-dependent: a Cut blend ignores its duration, Group Framing's FOV limits only matter in FOV modes, ...
	TStrongObjectPtr<UACFCameraPreset> Cut(MakePreset());
	AddNode<UACFCameraNode_FixedPose>(Cut.Get());
	Cut->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
	Cut->DefaultBlendIn.Duration = -1.f;
	TestNull(TEXT("Duration of a Cut blend is not checked"), Find(Validate(Cut.Get()), TEXT("ACF.Param.OutOfRange")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationFovTest, "ActionCameraFramework.PresetValidation.InvalidFieldOfView", ACFTests::Flags)
bool FACFPresetValidationFovTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_FixedPose* Wide = AddNode<UACFCameraNode_FixedPose>(Preset.Get());
	Wide->bOverrideLocation = false;
	Wide->bOverrideRotation = false;
	Wide->FieldOfView = 200.f; // the Details panel allows up to 360
	UACFCameraNode_LockOn* LockOn = AddNode<UACFCameraNode_LockOn>(Preset.Get());
	LockOn->NearFieldOfView = 0.f; // unused: Adjust Field Of View is off
	TArray<FACFCameraPresetIssue> Issues = Validate(Preset.Get());
	AddInfo(Describe(Issues));
	const FACFCameraPresetIssue* Fov = Find(Issues, TEXT("ACF.Param.InvalidFov"), TEXT("FieldOfView"));
	TestTrue(TEXT("FOV 200 is an error"), Fov && Fov->Severity == ESeverity::Error && Fov->Node == Wide);
	TestNull(TEXT("FOV of a disabled option is not checked"), Find(Issues, TEXT("ACF.Param.InvalidFov"), TEXT("NearFieldOfView")));

	LockOn->bAdjustFieldOfView = true;
	Wide->FieldOfView = 0.f;
	Issues = Validate(Preset.Get());
	TestNotNull(TEXT("FOV 0 is an error"), Find(Issues, TEXT("ACF.Param.InvalidFov"), TEXT("FieldOfView")));
	TestNotNull(TEXT("Lock-On near FOV 0 is an error once enabled"), Find(Issues, TEXT("ACF.Param.InvalidFov"), TEXT("NearFieldOfView")));

	Wide->FieldOfView = 179.f;
	LockOn->NearFieldOfView = 90.f;
	Issues = Validate(Preset.Get());
	TestEqual(TEXT("179 degrees is wide but valid"), CountId(Issues, TEXT("ACF.Param.InvalidFov")), 0);

	TStrongObjectPtr<UACFCameraPreset> Narrow(MakePreset());
	UACFCameraNode_GroupFraming* Framing = AddNode<UACFCameraNode_GroupFraming>(Narrow.Get());
	Framing->MinFieldOfView = 2.f; // valid for UE, below the node's supported range
	Issues = Validate(Narrow.Get());
	const FACFCameraPresetIssue* Range = Find(Issues, TEXT("ACF.Param.OutOfRange"), TEXT("MinFieldOfView"));
	TestTrue(TEXT("Below the node range: warning, not error"), Range && Range->Severity == ESeverity::Warning);
	TestEqual(TEXT("No FOV error"), CountId(Issues, TEXT("ACF.Param.InvalidFov")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationMinMaxTest, "ActionCameraFramework.PresetValidation.MinMaxInverted", ACFTests::Flags)
bool FACFPresetValidationMinMaxTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Orbit(MakePreset());
	UACFCameraNode_OrbitalFollow* OrbitNode = AddNode<UACFCameraNode_OrbitalFollow>(Orbit.Get());
	OrbitNode->PitchMin = 30.f;
	OrbitNode->PitchMax = -60.f;
	AddNode<UACFCameraNode_Collision>(Orbit.Get());
	TArray<FACFCameraPresetIssue> Issues = Validate(Orbit.Get());
	AddInfo(Describe(Issues));
	const FACFCameraPresetIssue* Pitch = Find(Issues, TEXT("ACF.Param.MinMaxInverted"));
	TestTrue(TEXT("Orbit pitch limits inverted"), Pitch && Pitch->Severity == ESeverity::Warning && Pitch->Property == TEXT("PitchMin / PitchMax"));

	TStrongObjectPtr<UACFCameraPreset> Group(MakePreset());
	UACFCameraNode_GroupFraming* Framing = AddNode<UACFCameraNode_GroupFraming>(Group.Get());
	Framing->MinFieldOfView = 100.f;
	Framing->MaxFieldOfView = 30.f;
	UACFCameraNode_LockOn* LockOn = AddNode<UACFCameraNode_LockOn>(Group.Get());
	LockOn->NearRange = 2000.f;
	LockOn->FarRange = 200.f;
	LockOn->BasePitch = 50.f; // above PitchMax 30
	UACFCameraNode_Combat* Combat = AddNode<UACFCameraNode_Combat>(Group.Get());
	Combat->Profile.NearRange = 1500.f;
	Combat->Profile.FarRange = 1500.f;
	UACFCameraNode_RotationComposer* Composer = AddNode<UACFCameraNode_RotationComposer>(Group.Get());
	Composer->bEnabled = false; // relations are only checked on enabled nodes
	Composer->Composition.SoftZoneSize = FVector2D(0.05, 0.05);
	Issues = Validate(Group.Get());
	AddInfo(Describe(Issues));
	TestTrue(TEXT("Group Framing FOV limits inverted"), Issues.ContainsByPredicate([Framing](const FACFCameraPresetIssue& Issue) { return Issue.Id == TEXT("ACF.Param.MinMaxInverted") && Issue.Node == Framing; }));
	TestTrue(TEXT("Lock-On near / far range inverted"), Issues.ContainsByPredicate([LockOn](const FACFCameraPresetIssue& Issue) { return Issue.Id == TEXT("ACF.Param.RangeInverted") && Issue.Node == LockOn; }));
	TestTrue(TEXT("Combat near == far range"), Issues.ContainsByPredicate([Combat](const FACFCameraPresetIssue& Issue) { return Issue.Id == TEXT("ACF.Param.RangeInverted") && Issue.Node == Combat; }));
	const FACFCameraPresetIssue* Base = Find(Issues, TEXT("ACF.Param.Normalized"), TEXT("BasePitch"));
	TestTrue(TEXT("Base pitch outside the limits is clamped (info)"), Base && Base->Severity == ESeverity::Info);
	TestEqual(TEXT("Disabled composer: no relation checks"), CountId(Issues, TEXT("ACF.Param.Normalized")), 1);

	Composer->bEnabled = true;
	Issues = Validate(Group.Get());
	const FACFCameraPresetIssue* Soft = Find(Issues, TEXT("ACF.Param.Normalized"), TEXT("Composition.SoftZoneSize"));
	TestTrue(TEXT("Soft zone below dead zone is widened at runtime (info)"), Soft && Soft->Severity == ESeverity::Info && Soft->Node == Composer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationNoCollisionTest, "ActionCameraFramework.PresetValidation.NoCollisionIsAllowed", ACFTests::Flags)
bool FACFPresetValidationNoCollisionTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Orbit(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Orbit.Get());
	const TArray<FACFCameraPresetIssue> Issues = Validate(Orbit.Get());
	TestTrue(TEXT("Only a note"), Issues.Num() == 1 && Issues[0].Id == TEXT("ACF.Collision.None") && Issues[0].Severity == ESeverity::Info);
	FDataValidationContext Context;
	TestEqual(TEXT("Valid for Data Validation"), Orbit->IsDataValid(Context), EDataValidationResult::Valid);
	TestEqual(TEXT("No warnings"), static_cast<int32>(Context.GetNumWarnings()), 0);
	TestEqual(TEXT("The note reaches Data Validation as Info"), static_cast<int32>(Algo::CountIf(Context.GetIssues(), [](const FDataValidationContext::FIssue& Issue) { return Issue.Severity == EMessageSeverity::Info; })), 1);

	// Fixed shots / zoom presets do not even get the note: nothing moves the camera around a target.
	TStrongObjectPtr<UACFCameraPreset> Fixed(MakeFixedPreset(FVector(0, 0, 500), 60.f));
	TestEqual(TEXT("Fixed Pose without Collision: nothing to report"), Validate(Fixed.Get()).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationTransitionTest, "ActionCameraFramework.PresetValidation.CollisionTransitions", ACFTests::Flags)
bool FACFPresetValidationTransitionTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Walled(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Walled.Get());
	UACFCameraNode_Collision* WalledCollision = AddNode<UACFCameraNode_Collision>(Walled.Get());
	TStrongObjectPtr<UACFCameraPreset> Open(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Open.Get());
	TStrongObjectPtr<UACFCameraPreset> Thick(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Thick.Get());
	UACFCameraNode_Collision* ThickCollision = AddNode<UACFCameraNode_Collision>(Thick.Get());
	ThickCollision->ProbeRadius = 40.f;

	TArray<FACFCameraPresetIssue> Issues;
	ACFCameraPresetValidation::ValidateTransition(Walled.Get(), Open.Get(), Issues);
	TestTrue(TEXT("Collision -> none: the constraint is dropped from the start of the blend"), Issues.Num() == 1 && Issues[0].Id == TEXT("ACF.Transition.CollisionDropped")
		&& Issues[0].Severity == ESeverity::Warning && Issues[0].Preset == Open.Get());
	Issues.Reset();
	ACFCameraPresetValidation::ValidateTransition(Open.Get(), Walled.Get(), Issues);
	TestTrue(TEXT("None -> collision: info"), Issues.Num() == 1 && Issues[0].Id == TEXT("ACF.Transition.CollisionAdded") && Issues[0].Severity == ESeverity::Info && Issues[0].Node == WalledCollision);
	Issues.Reset();
	ACFCameraPresetValidation::ValidateTransition(Walled.Get(), Thick.Get(), Issues);
	TestTrue(TEXT("Different probes: info naming the probe used"), Issues.Num() == 1 && Issues[0].Id == TEXT("ACF.Transition.CollisionSettingsDiffer") && Issues[0].Node == ThickCollision);
	Issues.Reset();
	ThickCollision->ProbeRadius = WalledCollision->ProbeRadius;
	ACFCameraPresetValidation::ValidateTransition(Walled.Get(), Thick.Get(), Issues);
	TestEqual(TEXT("Same collision settings: nothing to note"), Issues.Num(), 0);

	// Single-preset validation never reports transitions (a preset does not know what blends into it).
	TestEqual(TEXT("Open preset alone: only the no-collision note"), Validate(Open.Get()).Num(), 1);

	// Sequences know their transitions: consecutive blended shots get the notes, cuts do not.
	TStrongObjectPtr<UACFCameraSequence> Sequence(NewObject<UACFCameraSequence>(GetTransientPackage(), NAME_None, RF_Transient));
	FACFCameraShot& First = Sequence->Shots.AddDefaulted_GetRef();
	First.Preset = Walled.Get();
	First.Duration = 1.f;
	FACFCameraShot& Second = Sequence->Shots.AddDefaulted_GetRef();
	Second.Preset = Open.Get();
	Second.Duration = 1.f;
	Second.BlendIn = Linear(0.4f);
	auto CountTransitionNotes = [](const FDataValidationContext& Context)
	{
		return static_cast<int32>(Algo::CountIf(Context.GetIssues(), [](const FDataValidationContext::FIssue& Issue)
		{
			const FText Text = Issue.TokenizedMessage ? Issue.TokenizedMessage->ToText() : Issue.Message;
			return Issue.Severity == EMessageSeverity::Info && Text.ToString().Contains(TEXT("ACF.Transition.CollisionDropped"));
		}));
	};
	{
		FDataValidationContext Context;
		Sequence->IsDataValid(Context);
		TestEqual(TEXT("Blended shot transition is noted"), CountTransitionNotes(Context), 1);
		TestEqual(TEXT("Existing per-shot warning unchanged (shot 1 has no Collision)"), static_cast<int32>(Context.GetNumWarnings()), 1);
	}
	Sequence->Shots[1].BlendIn = FACFCameraBlendSettings::MakeCut();
	{
		FDataValidationContext Context;
		Sequence->IsDataValid(Context);
		TestEqual(TEXT("Cut between shots: no transition note"), CountTransitionNotes(Context), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationReadOnlyTest, "ActionCameraFramework.PresetValidation.DoesNotModifyThePreset", ACFTests::Flags)
bool FACFPresetValidationReadOnlyTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->PitchMin = 30.f;
	Orbit->PitchMax = -60.f;
	Orbit->RotationDamping = FVector2D(-1.0, std::numeric_limits<double>::quiet_NaN());
	UACFCameraNode_FixedPose* Fixed = AddNode<UACFCameraNode_FixedPose>(Preset.Get());
	Fixed->FieldOfView = 400.f;
	Preset->Nodes.Add(nullptr);
	Preset->DefaultBlendIn.Duration = -3.f;

	const TArray<UACFCameraNode*> NodesBefore(Preset->Nodes);
	const TArray<uint8> PresetBefore = Snapshot(Preset.Get());
	const TArray<uint8> OrbitBefore = Snapshot(Orbit);
	const TArray<uint8> FixedBefore = Snapshot(Fixed);
	const bool bDirtyBefore = Preset->GetPackage()->IsDirty();

	const TArray<FACFCameraPresetIssue> Issues = Validate(Preset.Get());
	FDataValidationContext Context;
	Preset->IsDataValid(Context);
	TArray<FACFCameraPresetIssue> Transition;
	ACFCameraPresetValidation::ValidateTransition(Preset.Get(), MakeFixedPreset(FVector::ZeroVector, 90.f), Transition);

	TestTrue(TEXT("It found problems"), Count(Issues, ESeverity::Error) > 0 && Count(Issues, ESeverity::Warning) > 0);
	TestTrue(TEXT("Node list unchanged"), TArray<UACFCameraNode*>(Preset->Nodes) == NodesBefore);
	TestTrue(TEXT("Preset bytes unchanged"), Snapshot(Preset.Get()) == PresetBefore);
	TestTrue(TEXT("Orbit bytes unchanged (inverted pitch and NaN kept)"), Snapshot(Orbit) == OrbitBefore);
	TestTrue(TEXT("Fixed Pose bytes unchanged (FOV 400 kept)"), Snapshot(Fixed) == FixedBefore);
	TestEqual(TEXT("Package dirty state unchanged"), Preset->GetPackage()->IsDirty(), bDirtyBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationPoseTest, "ActionCameraFramework.PresetValidation.ValidPresetPoseUnchanged", ACFTests::Flags)
bool FACFPresetValidationPoseTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetValidationTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->PivotDamping = FACFCameraPositionDamping::Uniform(0.2f, EACFCameraDampingSpace::CameraYaw);
	Orbit->RotationDamping = FVector2D(0.1, 0.1);
	AddNode<UACFCameraNode_LookAt>(Preset.Get())->TargetSource = EACFCameraLookAtSource::FollowTarget;
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(100, 200, 0)));
	FACFCameraRequestParams Params;
	Params.FollowTarget.Component = Target.Get();

	auto Run = [&](bool bValidateEveryFrame)
	{
		Target->SetWorldLocation(FVector(100, 200, 0));
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		Director->AddRequest(Preset.Get(), Params);
		FFrameDriver Driver;
		TArray<FACFCameraPose> Poses;
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			if (bValidateEveryFrame)
			{
				Validate(Preset.Get());
				FDataValidationContext Context;
				Preset->IsDataValid(Context);
			}
			Target->SetWorldLocation(FVector(100 + Frame * 4.0, 200, 0));
			Director->AddCameraInput(FVector2D(Frame < 30 ? 1.0 : 0.0, 0.0));
			Poses.Add(Driver.Step(Director.Get(), 1 / 60.f).Pose);
		}
		return Poses;
	};
	const TArray<FACFCameraPose> Plain = Run(false);
	const TArray<FACFCameraPose> Validated = Run(true);
	bool bSame = Plain.Num() == Validated.Num();
	for (int32 Index = 0; bSame && Index < Plain.Num(); ++Index)
	{
		bSame = Plain[Index].Location == Validated[Index].Location && Plain[Index].Rotation == Validated[Index].Rotation && Plain[Index].FieldOfView == Validated[Index].FieldOfView;
	}
	TestTrue(TEXT("Bit-identical poses with validation running in between"), bSame);
	TestTrue(TEXT("The camera actually moved"), !Plain[0].Location.Equals(Plain.Last().Location, 1.0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
