#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "ACFCameraTuning.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Serialization/ObjectWriter.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

/** Phase 7A-1: runtime camera tuning (ACFCameraTuning.h, README 24 章). Director level, fixed DeltaTime. */
namespace ACFTuningTests
{
	using namespace ACFTests;
	using EParam = EACFCameraTuningParameter;
	using EResult = EACFCameraTuningResult;

	inline UACFCameraPreset* MakePreset()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	template <typename T>
	T* AddNode(UACFCameraPreset* Preset)
	{
		T* Node = NewObject<T>(Preset);
		Preset->Nodes.Add(Node);
		return Node;
	}

	/** Follow (offset -300, 0, 150, no damping) + FOV-only Fixed Pose (90) + Collision: one node for each parameter. */
	inline UACFCameraPreset* MakeFollowPreset()
	{
		UACFCameraPreset* Preset = MakePreset();
		AddNode<UACFCameraNode_Follow>(Preset);
		UACFCameraNode_FixedPose* Lens = AddNode<UACFCameraNode_FixedPose>(Preset);
		Lens->bOverrideLocation = false;
		Lens->bOverrideRotation = false;
		Lens->FieldOfView = 90.f;
		AddNode<UACFCameraNode_Collision>(Preset);
		return Preset;
	}

	inline USceneComponent* MakeTarget(const FVector& Location)
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetWorldLocation(Location);
		return Component;
	}

	inline FACFCameraRequestParams FollowParams(USceneComponent* Target, USceneComponent* Look = nullptr)
	{
		FACFCameraRequestParams Params;
		Params.FollowTarget.Component = Target;
		if (Look)
		{
			Params.LookAtTarget.Component = Look;
		}
		return Params;
	}

	inline TArray<uint8> Snapshot(const UObject* Object)
	{
		TArray<uint8> Bytes;
		FObjectWriter Writer(const_cast<UObject*>(Object), Bytes);
		return Bytes;
	}

	inline bool SamePose(const FACFCameraPose& A, const FACFCameraPose& B)
	{
		return A.Location == B.Location && A.Rotation == B.Rotation && A.FieldOfView == B.FieldOfView;
	}

	/** Steps with an explicit World (collision needs one). */
	struct FWorldStepper
	{
		uint64 Frame = 9000000;
		const FACFCameraEvaluationResult& Step(UACFCameraDirector* Director, UWorld* World, float Dt = 1.f / 60.f)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = Dt;
			Context.FrameNumber = ++Frame;
			Context.World = World;
			return Director->Evaluate(Context);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningCopyTest, "ActionCameraFramework.Tuning.CopyIsIndependentAndDiscarded", ACFTests::Flags)
bool FACFTuningCopyTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	Driver.Step(Director.Get(), 1 / 60.f);

	TArray<TArray<uint8>> Before;
	Before.Add(Snapshot(Preset.Get()));
	for (const UACFCameraNode* Node : Preset->Nodes)
	{
		Before.Add(Snapshot(Node));
	}

	TestEqual(TEXT("Start (invalid handle = the active request)"), Director->StartTuning(FACFCameraRequestHandle()), EResult::Ok);
	const UACFCameraPreset* Copy = Director->GetRequestPreset(Handle);
	if (!TestTrue(TEXT("The request runs on its own copy"), Copy && Copy != Preset.Get()))
	{
		return false;
	}
	TestTrue(TEXT("Copy is transient and owned by the director"), Copy->HasAnyFlags(RF_Transient) && Copy->GetOuter() == Director.Get());
	TestEqual(TEXT("Same node layout"), Copy->Nodes.Num(), Preset->Nodes.Num());
	for (int32 Index = 0; Index < Copy->Nodes.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("Node %d: a new object of the same class, owned by the copy"), Index), Copy->Nodes[Index] && Copy->Nodes[Index] != Preset->Nodes[Index]
			&& Copy->Nodes[Index]->GetClass() == Preset->Nodes[Index]->GetClass() && Copy->Nodes[Index]->GetOuter() == Copy);
	}

	TestEqual(TEXT("Set damping"), Director->SetTuningValue(Handle, EParam::FollowDamping, FVector(0.3, 0.3, 0.3)), EResult::Ok);
	TestEqual(TEXT("Set FOV"), Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(60.0, 0.0, 0.0)), EResult::Ok);
	TestEqual(TEXT("Set offset"), Director->SetTuningValue(Handle, EParam::CameraOffset, FVector(-500.0, 0.0, 200.0)), EResult::Ok);
	TestEqual(TEXT("Set collision radius"), Director->SetTuningValue(Handle, EParam::CollisionRadius, FVector(30.0, 0.0, 0.0)), EResult::Ok);
	Driver.Step(Director.Get(), 1 / 60.f);
	const FACFCameraTuningStatus Status = Director->GetTuningStatus(Handle);
	TestTrue(TEXT("Status: tuning, 4 values changed"), Status.bTuning && Status.NumChangedValues == 4 && Status.OriginalPreset == Preset->GetFName() && Status.NumTunedRequests == 1);
	FACFCameraTuningValue Value;
	TestEqual(TEXT("Get"), Director->GetTuningValue(Handle, EParam::CameraOffset, INDEX_NONE, Value), EResult::Ok);
	TestTrue(TEXT("Get: current and asset values, node and property"), Value.Value == FVector(-500.0, 0.0, 200.0) && Value.OriginalValue == FVector(-300.0, 0.0, 150.0)
		&& Value.NodeIndex == 0 && Value.Property == TEXT("Offset") && !Value.bScalar);

	bool bUnchanged = Snapshot(Preset.Get()) == Before[0];
	for (int32 Index = 0; Index < Preset->Nodes.Num(); ++Index)
	{
		bUnchanged &= Snapshot(Preset->Nodes[Index]) == Before[Index + 1];
	}
	TestTrue(TEXT("The shared preset and its nodes are byte-identical"), bUnchanged);
	TestEqual(TEXT("Asset Follow offset untouched"), CastChecked<UACFCameraNode_Follow>(Preset->Nodes[0])->Offset, FVector(-300.0, 0.0, 150.0));

	TestEqual(TEXT("Start again: Ok"), Director->StartTuning(Handle), EResult::Ok);
	TestTrue(TEXT("Start again keeps the same copy and its values"), Director->GetRequestPreset(Handle) == Copy && Director->GetTuningStatus(Handle).NumChangedValues == 4);

	const FWeakObjectPtr WeakCopy(const_cast<UACFCameraPreset*>(Copy));
	const FWeakObjectPtr WeakNode(Copy->Nodes[0].Get());
	TestEqual(TEXT("Stop"), Director->StopTuning(Handle), EResult::Ok);
	TestTrue(TEXT("Stop: the request is back on the shared preset"), Director->GetRequestPreset(Handle) == Preset.Get());
	TestFalse(TEXT("Stop: not tuning"), Director->GetTuningStatus(Handle).bTuning);
	TestEqual(TEXT("Stop: no session left"), Director->GetNumTunedRequests(), 0);
	Driver.Step(Director.Get(), 1 / 60.f);
	TestEqual(TEXT("After stop the asset values are used again (FOV 90)"), Director->GetLastResult().Pose.FieldOfView, 90.f);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("The copy and its nodes are released by the GC"), WeakCopy.Get(true) == nullptr && WeakNode.Get(true) == nullptr);
	TestTrue(TEXT("Still byte-identical after the whole session"), Snapshot(Preset.Get()) == Before[0] && Snapshot(Preset->Nodes[0]) == Before[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningEffectTest, "ActionCameraFramework.Tuning.ParametersTakeEffectAndReset", ACFTests::Flags)
bool FACFTuningEffectTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	FACFCameraPose Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Asset: offset (-300, 0, 150), FOV 90"), Pose.Location.Equals(FVector(-300.0, 0.0, 150.0), 1e-3) && Pose.FieldOfView == 90.f);
	TestEqual(TEXT("Start"), Director->StartTuning(Handle), EResult::Ok);

	// Offset and FOV: the next update uses them (Follow without damping, FOV-only Fixed Pose).
	Director->SetTuningValue(Handle, EParam::CameraOffset, FVector(-500.0, 0.0, 200.0));
	Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(60.0, 0.0, 0.0));
	Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(FString::Printf(TEXT("Offset applied (%s)"), *Pose.Location.ToString()), Pose.Location.Equals(FVector(-500.0, 0.0, 200.0), 1e-3));
	TestEqual(TEXT("FOV applied"), Pose.FieldOfView, 60.f);

	// Damping: a constant-velocity target is trailed by Velocity x DampingTime per axis (the damping model of the Follow node).
	const double Speed = 600.0;
	auto Run = [&](int32 Frames)
	{
		for (int32 I = 0; I < Frames; ++I)
		{
			Target->SetWorldLocation(Target->GetComponentLocation() + FVector(Speed / 60.0, 0.0, 0.0));
			Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
		}
		return Target->GetComponentLocation().X + (-500.0) - Pose.Location.X; // extra trail behind the offset
	};
	TestTrue(TEXT("No damping: no trail"), FMath::IsNearlyEqual(Run(60), 0.0, 1e-3));
	TestEqual(TEXT("Set damping X = 0.5 s"), Director->SetTuningValue(Handle, EParam::FollowDamping, FVector(0.5, 0.0, 0.0)), EResult::Ok);
	const double Trail = Run(300);
	TestTrue(FString::Printf(TEXT("Damping applied: trail %.2f cm = speed x 0.5 s = 300"), Trail), FMath::IsNearlyEqual(Trail, Speed * 0.5, 1.0));

	// Reset one value, then all.
	TestEqual(TEXT("Reset FOV"), Director->ResetTuningValue(Handle, EParam::FieldOfView), EResult::Ok);
	Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestEqual(TEXT("FOV back to the asset value"), Pose.FieldOfView, 90.f);
	TestEqual(TEXT("Two values still changed"), Director->GetTuningStatus(Handle).NumChangedValues, 2);
	TestEqual(TEXT("Reset all"), Director->ResetAllTuningValues(Handle), EResult::Ok);
	TestEqual(TEXT("Nothing changed any more"), Director->GetTuningStatus(Handle).NumChangedValues, 0);
	TestTrue(TEXT("Still tuning after Reset All"), Director->GetTuningStatus(Handle).bTuning);
	Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(FString::Printf(TEXT("Offset back to (-300, 0, 150) with no damping (%s)"), *Pose.Location.ToString()),
		Pose.Location.Equals(Target->GetComponentLocation() + FVector(-300.0, 0.0, 150.0), 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningCollisionTest, "ActionCameraFramework.Tuning.CollisionRadiusTakesEffect", ACFTests::Flags)
bool FACFTuningCollisionTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("test world created"), Wrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	// Wall 300 cm behind the pivot (face at X = -290), the orbit wants the camera 600 cm behind.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Wall = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(-300.0, 0.0, 0.0)), SpawnParams);
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("Box"));
	Box->SetBoxExtent(FVector(10.0, 400.0, 400.0));
	Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Wall->SetRootComponent(Box);
	Box->RegisterComponent();
	Wall->SetActorLocation(FVector(-300.0, 0.0, 0.0));

	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->InputSource = EACFCameraOrbitInputSource::None;
	Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
	Orbit->InitialYaw = 0.f;
	Orbit->InitialPitch = 0.f;
	Orbit->PivotOffset = FVector::ZeroVector;
	Orbit->Distance = 600.f;
	UACFCameraNode_Collision* Collision = AddNode<UACFCameraNode_Collision>(Preset.Get());
	Collision->ProbeRadius = 12.f;
	Collision->ReturnDampingTime = 0.f;
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FWorldStepper Stepper;
	double X = 0.0;
	for (int32 I = 0; I < 3; ++I)
	{
		X = Stepper.Step(Director.Get(), World).Pose.Location.X;
	}
	TestTrue(FString::Printf(TEXT("Radius 12: the camera stops 12 cm in front of the wall (X %.2f)"), X), FMath::IsNearlyEqual(X, -290.0 + 12.0, 0.5));
	TestEqual(TEXT("Start"), Director->StartTuning(Handle), EResult::Ok);
	TestEqual(TEXT("Radius 40"), Director->SetTuningValue(Handle, EParam::CollisionRadius, FVector(40.0, 0.0, 0.0)), EResult::Ok);
	for (int32 I = 0; I < 3; ++I)
	{
		X = Stepper.Step(Director.Get(), World).Pose.Location.X;
	}
	TestTrue(FString::Printf(TEXT("Radius 40: 40 cm in front of the wall (X %.2f)"), X), FMath::IsNearlyEqual(X, -290.0 + 40.0, 0.5));
	TestTrue(TEXT("The output guard uses the tuned radius too"), Director->GetOutputConstraint().bValid && FMath::IsNearlyEqual(Director->GetOutputConstraint().ProbeRadius, 40.f));
	Director->StopTuning(Handle);
	for (int32 I = 0; I < 3; ++I)
	{
		X = Stepper.Step(Director.Get(), World).Pose.Location.X;
	}
	TestTrue(FString::Printf(TEXT("Stopped: back to radius 12 (X %.2f)"), X), FMath::IsNearlyEqual(X, -290.0 + 12.0, 0.5));
	TestEqual(TEXT("Asset radius untouched"), Collision->ProbeRadius, 12.f);
	Wrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningStateTest, "ActionCameraFramework.Tuning.CameraStateIsKept", ACFTests::Flags)
bool FACFTuningStateTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	// Two directors fed exactly the same: B is never tuned (the reference). Orbit with input and pivot damping (state: orbit
	// angles, damping history), a Lock-On request blending in over it (state: lock point, arm, blend), an effect running.
	TStrongObjectPtr<UACFCameraPreset> OrbitPreset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(OrbitPreset.Get());
	Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
	Orbit->PivotDamping = FACFCameraPositionDamping::Uniform(0.3f, EACFCameraDampingSpace::CameraYaw);
	Orbit->RotationDamping = FVector2D(0.2, 0.2);
	Orbit->ShoulderOffset = FVector(0.0, 40.0, 20.0);
	TStrongObjectPtr<UACFCameraPreset> LockPreset(MakePreset());
	AddNode<UACFCameraNode_LockOn>(LockPreset.Get());
	LockPreset->DefaultBlendIn = Linear(1.f);
	TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	UACFCameraEffect_Punch* Punch = NewObject<UACFCameraEffect_Punch>(Effect.Get());
	Punch->Rotation = FRotator(3.f, 0.f, 0.f);
	Punch->Envelope.Attack = 0.5f;
	Punch->Envelope.Sustain = 2.f;
	Punch->Envelope.Release = 0.5f;
	Effect->Layers.Add(Punch);

	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(800.0, 300.0, 0.0)));
	TStrongObjectPtr<UACFCameraDirector> A(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> B(NewObject<UACFCameraDirector>());
	FACFCameraRequestHandle OrbitA, OrbitB, LockA, LockB;
	OrbitA = A->AddRequest(OrbitPreset.Get(), FollowParams(Player.Get()));
	OrbitB = B->AddRequest(OrbitPreset.Get(), FollowParams(Player.Get()));
	FFrameDriver DriverA;
	FFrameDriver DriverB;
	bool bSame = true;
	int32 Frame = 0;
	auto Step = [&](int32 Frames, bool bCompare)
	{
		for (int32 I = 0; I < Frames; ++I, ++Frame)
		{
			Player->SetWorldLocation(FVector(Frame * 5.0, FMath::Sin(Frame * 0.05) * 200.0, 0.0));
			A->AddCameraInput(FVector2D(0.8, 0.1));
			B->AddCameraInput(FVector2D(0.8, 0.1));
			const FACFCameraPose PA = DriverA.Step(A.Get(), 1 / 60.f).Pose;
			const FACFCameraPose PB = DriverB.Step(B.Get(), 1 / 60.f).Pose;
			bSame &= !bCompare || SamePose(PA, PB);
		}
	};
	Step(60, true);
	A->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	B->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	Step(10, true);
	TestEqual(TEXT("Start tuning the orbit (damping, rotation, effect running)"), A->StartTuning(OrbitA), EResult::Ok);
	Step(30, true);
	TestTrue(TEXT("Orbit: bit-identical to the untuned director across Start (damping, orbit angles, effect kept)"), bSame);

	LockA = A->AddRequest(LockPreset.Get(), FollowParams(Player.Get(), Enemy.Get()));
	LockB = B->AddRequest(LockPreset.Get(), FollowParams(Player.Get(), Enemy.Get()));
	Step(20, true); // mid-blend (1 s)
	TestTrue(TEXT("Blending into Lock-On: still identical"), bSame && A->GetLastResult().bIsBlending);
	TestEqual(TEXT("Start tuning the Lock-On request mid-blend"), A->StartTuning(LockA), EResult::Ok);
	Step(80, true);
	TestTrue(TEXT("Lock-On + blend: bit-identical across Start (lock point, arm, blend progress kept)"), bSame);
	TestEqual(TEXT("Stop the Lock-On tuning"), A->StopTuning(LockA), EResult::Ok);
	TestEqual(TEXT("Stop the orbit tuning"), A->StopTuning(OrbitA), EResult::Ok);
	Step(30, true);
	TestTrue(TEXT("Bit-identical across Stop as well"), bSame);

	// A value change keeps the rest of the state: the shoulder offset moves the camera by exactly the rotated difference,
	// the rotation (orbit angles, damping) stays the reference one.
	A->RemoveRequest(LockA, nullptr, true);
	B->RemoveRequest(LockB, nullptr, true);
	A->StopAllEffects(true); // the punch rotation would otherwise be part of the compared rotation
	B->StopAllEffects(true);
	Step(5, false);
	A->StartTuning(OrbitA);
	A->SetTuningValue(OrbitA, EParam::CameraOffset, FVector(0.0, 90.0, 20.0)); // +50 to the right
	DriverA.Step(A.Get(), 1 / 60.f);
	DriverB.Step(B.Get(), 1 / 60.f);
	const FACFCameraPose PA = A->GetLastResult().Pose;
	const FACFCameraPose PB = B->GetLastResult().Pose;
	const FRotator OrbitRotation(PB.Rotation.Pitch, PB.Rotation.Yaw, 0.f);
	const FVector Expected = OrbitRotation.RotateVector(FVector(0.0, 50.0, 0.0));
	AddInfo(FString::Printf(TEXT("Shoulder +50: moved %s, expected %s"), *(PA.Location - PB.Location).ToString(), *Expected.ToString()));
	TestTrue(TEXT("Offset change: same rotation (orbit angles and rotation damping not reset)"), PA.Rotation.Equals(PB.Rotation, 1e-4f));
	TestTrue(TEXT("Offset change: moved by exactly the rotated offset difference (pivot damping not reset)"), (PA.Location - PB.Location).Equals(Expected, 1e-2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningInvalidTest, "ActionCameraFramework.Tuning.InvalidInputIsRejected", ACFTests::Flags)
bool FACFTuningInvalidTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeFollowPreset());
	TStrongObjectPtr<UACFCameraPreset> NoCollision(MakePreset());
	AddNode<UACFCameraNode_Follow>(NoCollision.Get());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	FFrameDriver Driver;
	FACFCameraTuningValue Value;
	TestEqual(TEXT("No request: Start"), Director->StartTuning(FACFCameraRequestHandle()), EResult::NoRequest);
	TestEqual(TEXT("No request: Get"), Director->GetTuningValue(FACFCameraRequestHandle(), EParam::FieldOfView, INDEX_NONE, Value), EResult::NoRequest);
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	Driver.Step(Director.Get(), 1 / 60.f);
	TestEqual(TEXT("Not tuning: Set"), Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(60.0, 0.0, 0.0)), EResult::NotTuning);
	TestEqual(TEXT("Not tuning: Stop"), Director->StopTuning(Handle), EResult::NotTuning);
	TestEqual(TEXT("Not tuning: Get works (asset value)"), Director->GetTuningValue(Handle, EParam::FieldOfView, INDEX_NONE, Value), EResult::Ok);
	TestEqual(TEXT("Start"), Director->StartTuning(Handle), EResult::Ok);

	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	TestEqual(TEXT("NaN offset"), Director->SetTuningValue(Handle, EParam::CameraOffset, FVector(NaN, 0.0, 0.0)), EResult::InvalidValue);
	TestEqual(TEXT("Infinite damping"), Director->SetTuningValue(Handle, EParam::FollowDamping, FVector(Inf, 0.0, 0.0)), EResult::InvalidValue);
	TestEqual(TEXT("Negative damping"), Director->SetTuningValue(Handle, EParam::FollowDamping, FVector(0.1, -0.1, 0.0)), EResult::InvalidValue);
	TestEqual(TEXT("FOV 0"), Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(0.0, 0.0, 0.0)), EResult::InvalidValue);
	TestEqual(TEXT("FOV 180"), Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(180.0, 0.0, 0.0)), EResult::InvalidValue);
	TestEqual(TEXT("Negative radius"), Director->SetTuningValue(Handle, EParam::CollisionRadius, FVector(-1.0, 0.0, 0.0)), EResult::InvalidValue);
	TestEqual(TEXT("Rejected values changed nothing"), Director->GetTuningStatus(Handle).NumChangedValues, 0);
	TestEqual(TEXT("Unknown parameter"), Director->SetTuningValue(Handle, static_cast<EParam>(99), FVector::ZeroVector), EResult::InvalidParameter);
	TestEqual(TEXT("Node index of a node without the parameter"), Director->SetTuningValue(Handle, EParam::CollisionRadius, FVector(10.0, 0.0, 0.0), 0), EResult::NoNode);
	TestEqual(TEXT("Node index out of range"), Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(60.0, 0.0, 0.0), 7), EResult::NoNode);
	TestEqual(TEXT("Explicit node index"), Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(60.0, 0.0, 0.0), 1), EResult::Ok);
	FACFCameraRequestHandle Stale = Handle;
	Stale.Invalidate();
	TestEqual(TEXT("Invalid handle = active request: works"), Director->SetTuningValue(Stale, EParam::FieldOfView, FVector(70.0, 0.0, 0.0)), EResult::Ok);

	const FACFCameraRequestHandle Other = Director->AddRequest(NoCollision.Get(), FollowParams(Target.Get()));
	Driver.Step(Director.Get(), 1 / 60.f);
	Director->StartTuning(Other);
	TestEqual(TEXT("Preset without Collision: radius has no node"), Director->SetTuningValue(Other, EParam::CollisionRadius, FVector(10.0, 0.0, 0.0)), EResult::NoNode);
	TestEqual(TEXT("Follow only: FOV has no node"), Director->SetTuningValue(Other, EParam::FieldOfView, FVector(60.0, 0.0, 0.0)), EResult::NoNode);
	// The removed active request is retired until the next evaluation starts the transition away from it; its session
	// follows the entry, so it is gone one evaluation later.
	Director->RemoveRequest(Other, nullptr, false);
	Driver.Step(Director.Get(), 1 / 60.f);
	Driver.Step(Director.Get(), 1 / 60.f);
	TestEqual(TEXT("Removed request: its session ends"), Director->GetNumTunedRequests(), 1);
	TestEqual(TEXT("Removed request: Set says NoRequest"), Director->SetTuningValue(Other, EParam::CameraOffset, FVector::ZeroVector), EResult::NoRequest);

	// The asset Fixed Pose with Override Field Of View off has no tunable FOV.
	UACFCameraNode_FixedPose* Lens = NewObject<UACFCameraNode_FixedPose>(NoCollision.Get());
	Lens->bOverrideFieldOfView = false;
	NoCollision->Nodes.Add(Lens);
	TestEqual(TEXT("FOV with its override off: no node"), ACFCameraTuning::FindNode(*NoCollision, EParam::FieldOfView, INDEX_NONE), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningPlayersTest, "ActionCameraFramework.Tuning.LocalPlayersAreIndependent", ACFTests::Flags)
bool FACFTuningPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> P1(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> P2(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle H1 = P1->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	const FACFCameraRequestHandle H2 = P2->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver D1;
	FFrameDriver D2;
	D1.Step(P1.Get(), 1 / 60.f);
	D2.Step(P2.Get(), 1 / 60.f);
	TestEqual(TEXT("P1 starts"), P1->StartTuning(H1), EResult::Ok);
	TestEqual(TEXT("P1 FOV 50"), P1->SetTuningValue(H1, EParam::FieldOfView, FVector(50.0, 0.0, 0.0)), EResult::Ok);
	TestEqual(TEXT("P1 offset"), P1->SetTuningValue(H1, EParam::CameraOffset, FVector(-100.0, 0.0, 0.0)), EResult::Ok);
	const FACFCameraPose Pose1 = D1.Step(P1.Get(), 1 / 60.f).Pose;
	const FACFCameraPose Pose2 = D2.Step(P2.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("P1 sees its values"), Pose1.FieldOfView == 50.f && Pose1.Location.Equals(FVector(-100.0, 0.0, 0.0), 1e-3));
	TestTrue(TEXT("P2 still sees the asset"), Pose2.FieldOfView == 90.f && Pose2.Location.Equals(FVector(-300.0, 0.0, 150.0), 1e-3));
	TestTrue(TEXT("P2 runs the shared preset, not tuning"), P2->GetRequestPreset(H2) == Preset.Get() && !P2->GetTuningStatus(H2).bTuning && P2->GetNumTunedRequests() == 0);
	TestEqual(TEXT("P2 cannot reach P1's request (handles are per player)"), P2->SetTuningValue(H1, EParam::FieldOfView, FVector(30.0, 0.0, 0.0)), EResult::NoRequest);
	TestEqual(TEXT("Both may tune at once"), P2->StartTuning(H2), EResult::Ok);
	TestTrue(TEXT("Each player has its own copy"), P1->GetRequestPreset(H1) != P2->GetRequestPreset(H2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningLifetimeTest, "ActionCameraFramework.Tuning.EndsWithTheWorldAndTheRequest", ACFTests::Flags)
bool FACFTuningLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	FTestWorldWrapper First;
	FTestWorldWrapper Second;
	if (!TestTrue(TEXT("test worlds created"), First.CreateTestWorld(EWorldType::Game) && Second.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FWorldStepper Stepper;
	Stepper.Step(Director.Get(), First.GetTestWorld());
	Director->StartTuning(Handle);
	Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(50.0, 0.0, 0.0));
	FWeakObjectPtr WeakCopy(const_cast<UACFCameraPreset*>(Director->GetRequestPreset(Handle)));
	TestEqual(TEXT("Same World: tuned FOV"), Stepper.Step(Director.Get(), First.GetTestWorld()).Pose.FieldOfView, 50.f);
	TestEqual(TEXT("World changed: the asset FOV in the first frame of the new World"), Stepper.Step(Director.Get(), Second.GetTestWorld()).Pose.FieldOfView, 90.f);
	TestFalse(TEXT("World changed: tuning ended"), Director->GetTuningStatus(Handle).bTuning);
	TestTrue(TEXT("World changed: the request is back on the shared preset"), Director->GetRequestPreset(Handle) == Preset.Get());
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("World changed: the copy is released"), WeakCopy.Get(true) == nullptr);

	// A destroyed World ends it too.
	Director->StartTuning(Handle);
	Stepper.Step(Director.Get(), Second.GetTestWorld());
	Second.DestroyTestWorld(/*bForceGarbageCollect*/ true);
	Stepper.Step(Director.Get(), nullptr);
	TestEqual(TEXT("Destroyed World: tuning ended"), Director->GetNumTunedRequests(), 0);

	// The request ends: the session (and the copy) go with it, after the request's blend-out.
	Director->StartTuning(Handle);
	Stepper.Step(Director.Get(), First.GetTestWorld());
	WeakCopy = const_cast<UACFCameraPreset*>(Director->GetRequestPreset(Handle));
	Director->RemoveRequest(Handle, nullptr, true);
	Stepper.Step(Director.Get(), First.GetTestWorld());
	Stepper.Step(Director.Get(), First.GetTestWorld());
	TestEqual(TEXT("Request removed: no session"), Director->GetNumTunedRequests(), 0);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("Request removed: the copy is released"), WeakCopy.Get(true) == nullptr);
	First.DestroyTestWorld(/*bForceGarbageCollect*/ false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningSwitchTest, "ActionCameraFramework.Tuning.RequestSwitchingKeepsTheTuning", ACFTests::Flags)
bool FACFTuningSwitchTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	TStrongObjectPtr<UACFCameraPreset> Gameplay(MakeFollowPreset());
	TStrongObjectPtr<UACFCameraPreset> Other(MakeFixedPreset(FVector(0.0, 0.0, 1000.0), 40.f));
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Base = Director->AddRequest(Gameplay.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	Driver.Step(Director.Get(), 1 / 60.f);
	Director->StartTuning(Base);
	Director->SetTuningValue(Base, EParam::FieldOfView, FVector(55.0, 0.0, 0.0));
	TestEqual(TEXT("Tuned FOV on screen"), Driver.Step(Director.Get(), 1 / 60.f).Pose.FieldOfView, 55.f);

	FACFCameraRequestParams High;
	High.Priority = 10;
	const FACFCameraRequestHandle Top = Director->AddRequest(Other.Get(), High);
	TestEqual(TEXT("Higher priority request on screen (priority rules unchanged)"), Driver.Step(Director.Get(), 1 / 60.f).Pose.FieldOfView, 40.f);
	TestTrue(TEXT("The tuned request is still tuned underneath"), Director->GetTuningStatus(Base).bTuning);
	TestFalse(TEXT("The invalid handle now means the new active request (not tuned)"), Director->GetTuningStatus(FACFCameraRequestHandle()).bTuning);
	TestEqual(TEXT("Values can be changed while it is not on screen"), Director->SetTuningValue(Base, EParam::FieldOfView, FVector(65.0, 0.0, 0.0)), EResult::Ok);
	Director->RemoveRequest(Top, nullptr, true);
	TestEqual(TEXT("Back to the tuned request: its latest value"), Driver.Step(Director.Get(), 1 / 60.f).Pose.FieldOfView, 65.f);
	TestEqual(TEXT("The other preset was never touched"), CastChecked<UACFCameraNode_FixedPose>(Other->Nodes[0])->FieldOfView, 40.f);
	return true;
}

#if ACF_WITH_PROFILER
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningProfilerTest, "ActionCameraFramework.Tuning.ProfilerReadsTheTunedRig", ACFTests::Flags)
bool FACFTuningProfilerTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningTests;
	// 7A-2 will show tuning next to the B-1 profiler: the per-node records of a tuned rig are the same as of the asset rig.
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeFollowPreset());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	Director->SetProfilerForced(true);
	Director->GetProfiler().Start(16);
	Driver.Step(Director.Get(), 1 / 60.f);
	const int32 SamplesBefore = Director->GetProfiler().GetLatestFrame()->NumNodeSamples;
	Director->StartTuning(Handle);
	Director->SetTuningValue(Handle, EParam::FieldOfView, FVector(60.0, 0.0, 0.0));
	Driver.Step(Director.Get(), 1 / 60.f);
	const FACFCameraProfileFrame* Frame = Director->GetProfiler().GetLatestFrame();
	TestTrue(TEXT("A frame was recorded while tuning"), Frame != nullptr);
	if (Frame)
	{
		TestEqual(TEXT("Same number of node samples as before tuning (Follow, Fixed Pose; Collision runs without a World too)"), Frame->NumNodeSamples, SamplesBefore);
		bool bFollow = false;
		for (int32 I = 0; I < Frame->NumNodeSamples; ++I)
		{
			bFollow |= Frame->Nodes[I].NodeName == UACFCameraNode_Follow::StaticClass()->GetFName();
		}
		TestTrue(TEXT("Node samples are named by node class (the copy's nodes are the same classes)"), bFollow);
	}
	TestEqual(TEXT("The evaluation result (pose) carries the tuned FOV"), Director->GetLastResult().Pose.FieldOfView, 60.f);
	Director->SetProfilerForced(false);
	return true;
}
#endif

#endif // WITH_DEV_AUTOMATION_TESTS
