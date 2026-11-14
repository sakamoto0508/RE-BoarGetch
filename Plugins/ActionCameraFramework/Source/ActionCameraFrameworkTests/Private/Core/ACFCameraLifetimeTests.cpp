#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "ACFCameraDirector.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "Effects/ACFCameraEffect.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Tests/AutomationCommon.h"
#include "UObject/ReferencerFinder.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

/**
 * Phase 6B / B-3: Actor の参照と寿命。実際の World（FTestWorldWrapper の Game World）・Actor・GC を使う。
 * 「コンテナから消えた」ではなく、GC の後に Actor / World のメモリが本当に解放されたか（TWeakObjectPtr::Get(true) が null）と、
 * Director が World の Actor を強参照していないか（FReferencerFinder）を確認する。
 * gc.GarbageEliminationEnabled（Garbage への強参照を GC が null にする機能）は ini / -DisableGarbageElimination で無効にでき、
 * Compact Set を既定にしたエンジンでは常に無効なので、どちらの設定でも成り立つことをテストする（README 17 章）。
 */
namespace ACFLifetimeTests
{
	using namespace ACFTests;

	static AActor* SpawnPoint(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	static AActor* SpawnBox(UWorld* World, const FVector& Center, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Center), Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Actor->SetActorLocation(Center);
		return Actor;
	}

	/** Orbit (fixed, 600 cm behind) + Look At + Collision (with the deoccluder checking the look-at target). */
	static UACFCameraPreset* MakeCollisionPreset(bool bDeoccluder)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Orbit->InputSource = EACFCameraOrbitInputSource::None;
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		Orbit->InitialYaw = 0.f;
		Orbit->InitialPitch = 0.f;
		Orbit->PivotOffset = FVector::ZeroVector;
		Orbit->Distance = 600.f;
		UACFCameraNode_LookAt* Look = NewObject<UACFCameraNode_LookAt>(Preset);
		Look->TargetSource = EACFCameraLookAtSource::LookAtTarget;
		UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
		Collision->Deoccluder.bEnabled = bDeoccluder;
		Collision->Deoccluder.bCheckLookAtTarget = bDeoccluder;
		Preset->Nodes = { Orbit, Look, Collision };
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	struct FStepper
	{
		uint64 Frame = 7000000;
		const FACFCameraEvaluationResult& Step(UACFCameraDirector* Director, UWorld* World, float Dt = 1.f / 60.f)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = Dt;
			Context.FrameNumber = ++Frame;
			Context.World = World;
			return Director->Evaluate(Context);
		}
	};

	/** True if the object's memory was released by the GC (not merely marked as garbage / destroyed). */
	static bool IsReleased(const FWeakObjectPtr& Weak)
	{
		return Weak.Get(/*bEvenIfGarbage*/ true) == nullptr;
	}

	static bool IsReferencedBy(UObject* Object, const UObject* Referencer)
	{
		const TArray<UObject*> Referencers = FReferencerFinder::GetAllReferencers(TArray<UObject*>{ Object }, nullptr, EReferencerFinderFlags::SkipWeakReferences); // strong references only
		return Referencers.Contains(Referencer);
	}

	static FString GcMode()
	{
		return UObjectBaseUtility::IsGarbageEliminationEnabled() ? TEXT("garbage elimination ON") : TEXT("garbage elimination OFF");
	}
}

// -------------------------------------------------------------------------------------------------
// 無視リストの Actor: Director は強参照しない。破棄 + GC でメモリが解放され、その後も Collision の評価は続く。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLifetimeIgnoredActorTest, "ActionCameraFramework.Lifetime.IgnoredActorIsNotKeptAlive", ACFTests::Flags)
bool FACFLifetimeIgnoredActorTest::RunTest(const FString& Parameters)
{
	using namespace ACFLifetimeTests;
	AddInfo(GcMode());
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("test world created"), Wrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	AActor* Player = SpawnPoint(World, FVector(0.0, 0.0, 200.0));
	AActor* Look = SpawnPoint(World, FVector(1000.0, 0.0, 200.0));
	AActor* Doomed = SpawnBox(World, FVector(-300.0, 0.0, 200.0), FVector(20.0)); // between the pivot and the camera
	AActor* Kept = SpawnBox(World, FVector(-450.0, 0.0, 200.0), FVector(20.0));
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeCollisionPreset(false));
	FACFCameraRequestParams Params;
	Params.FollowTarget.Actor = Player;
	Params.LookAtTarget.Actor = Look;
	Params.CollisionIgnoredActors.Add(Doomed);
	Params.CollisionIgnoredActors.Add(Kept);
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), Params);
	FStepper Stepper;
	for (int32 I = 0; I < 5; ++I)
	{
		Stepper.Step(Director.Get(), World);
	}
	const FVector Behind(-600.0, 0.0, 200.0);
	TestTrue(FString::Printf(TEXT("Ignored boxes do not stop the camera (%s)"), *Director->GetLastResult().Pose.Location.ToString()),
		Director->GetLastResult().Pose.Location.Equals(Behind, 1.0));
	TestFalse(TEXT("The director does not hold a strong reference to an ignored actor"), IsReferencedBy(Kept, Director.Get()));

	FWeakObjectPtr DoomedWeak(Doomed);
	Doomed->Destroy();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(FString::Printf(TEXT("Destroyed ignored actor: memory released by the GC (%s)"), *GcMode()), IsReleased(DoomedWeak));

	for (int32 I = 0; I < 5; ++I)
	{
		Stepper.Step(Director.Get(), World);
	}
	TestTrue(TEXT("After the destroy: collision keeps working and still ignores the live actor"), Director->GetLastResult().Pose.Location.Equals(Behind, 1.0));
	const FACFCameraOutputConstraint& Constraint = Director->GetOutputConstraint();
	TestTrue(TEXT("Output guard ignore list: the live ignored actor only"), Constraint.bValid
		&& Constraint.IgnoredActors.ContainsByPredicate([Kept](const TWeakObjectPtr<const AActor>& A) { return A.Get() == Kept; })
		&& !Constraint.IgnoredActors.ContainsByPredicate([](const TWeakObjectPtr<const AActor>& A) { return !A.IsValid(); }));

	Director->RemoveRequest(Handle, nullptr, true);
	Stepper.Step(Director.Get(), World);
	TestFalse(TEXT("Request ended: no reference left either"), IsReferencedBy(Kept, Director.Get()));
	Wrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 要求・Finisher を出したまま World を破棄する（マップ移動 / PIE 終了と同じ）。World と Actor が解放され、Director は動き続ける。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLifetimeWorldDestroyedTest, "ActionCameraFramework.Lifetime.WorldDestroyedWithLiveRequests", ACFTests::Flags)
bool FACFLifetimeWorldDestroyedTest::RunTest(const FString& Parameters)
{
	using namespace ACFLifetimeTests;
	AddInfo(GcMode());
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>()); // outlives the world, like the subsystem
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeCollisionPreset(true));
	FStepper Stepper;
	FWeakObjectPtr WorldWeak;
	TArray<FWeakObjectPtr> ActorsWeak;
	{
		FTestWorldWrapper Wrapper;
		if (!TestTrue(TEXT("test world created"), Wrapper.CreateTestWorld(EWorldType::Game)))
		{
			return false;
		}
		UWorld* World = Wrapper.GetTestWorld();
		AActor* Player = SpawnPoint(World, FVector(0.0, 0.0, 200.0));
		AActor* Enemy = SpawnPoint(World, FVector(1000.0, 0.0, 200.0));
		AActor* Ignored = SpawnBox(World, FVector(-300.0, 0.0, 200.0), FVector(20.0));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = Player;
		Params.LookAtTarget.Actor = Enemy;
		Params.CollisionIgnoredActors.Add(Ignored);
		Director->AddRequest(Preset.Get(), Params);
		// Finisher: attacker / target are added to the shot's collision ignore list automatically.
		Director->PlayFinisherPreset(Preset.Get(), Player, Enemy, 0.f, FACFCameraBlendSettings::MakeCut(), FACFCameraBlendSettings::MakeCut(), 100);
		for (int32 I = 0; I < 10; ++I)
		{
			Stepper.Step(Director.Get(), World);
		}
		TestTrue(TEXT("Finisher running"), Director->GetNumRequests() >= 2);
		for (AActor* Actor : { Player, Enemy, Ignored })
		{
			TestFalse(FString::Printf(TEXT("Director holds no strong reference to %s"), *Actor->GetName()), IsReferencedBy(Actor, Director.Get()));
			ActorsWeak.Add(FWeakObjectPtr(Actor));
		}
		WorldWeak = FWeakObjectPtr(World);
		Wrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(FString::Printf(TEXT("World released after destruction while requests were alive (%s)"), *GcMode()), IsReleased(WorldWeak));
	int32 Released = 0;
	for (const FWeakObjectPtr& Weak : ActorsWeak)
	{
		Released += IsReleased(Weak) ? 1 : 0;
	}
	TestEqual(TEXT("Actors of the destroyed world released"), Released, ActorsWeak.Num());

	// The director outlives the world (like the subsystem across map travel): keeps evaluating without the old actors.
	for (int32 I = 0; I < 5; ++I)
	{
		const FACFCameraEvaluationResult& Result = Stepper.Step(Director.Get(), nullptr);
		TestTrue(TEXT("Evaluation after the world is gone stays valid"), FMath::IsFinite(Result.Pose.Location.X) && FMath::IsFinite(Result.Weight));
	}
	TestFalse(TEXT("No output guard constraint pointing at the old world"), Director->GetOutputConstraint().bValid
		&& Director->GetOutputConstraint().IgnoredActors.ContainsByPredicate([](const TWeakObjectPtr<const AActor>& A) { return A.IsValid(); }));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Deoccluder: 確定した遮蔽状態は、破棄された Actor のアドレスに新しい Actor が確保されても引き継がれない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLifetimeDeoccluderIdentityTest, "ActionCameraFramework.Lifetime.DeoccluderStateNotInheritedByNewActor", ACFTests::Flags)
bool FACFLifetimeDeoccluderIdentityTest::RunTest(const FString& Parameters)
{
	using namespace ACFLifetimeTests;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("test world created"), Wrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	const FVector Hidden(1000.0, 0.0, 200.0);
	AActor* Player = SpawnPoint(World, FVector(0.0, 0.0, 200.0));
	SpawnBox(World, Hidden, FVector(80.0)); // encloses the look-at target: it can never be revealed
	AActor* First = SpawnPoint(World, Hidden);
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeCollisionPreset(true));
	FACFCameraRequestParams Params;
	Params.FollowTarget.Actor = Player;
	Params.LookAtTarget.Actor = First;
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), Params);
	FStepper Stepper;
	for (int32 I = 0; I < 60; ++I)
	{
		Stepper.Step(Director.Get(), World);
	}
	TestEqual(TEXT("The enclosed target is confirmed hidden"), Director->GetLastSolverReport().NumOccluded, 1);

	// Between two camera updates (e.g. an enemy killed and respawned, or map travel): destroy, GC, spawn a new actor.
	const void* OldAddress = First;
	First->Destroy();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	AActor* Second = nullptr;
	TArray<AActor*> Extra;
	for (int32 Try = 0; Try < 64 && !Second; ++Try)
	{
		AActor* Candidate = SpawnPoint(World, Hidden);
		if (Candidate == OldAddress)
		{
			Second = Candidate;
		}
		else
		{
			Extra.Add(Candidate);
		}
	}
	const bool bReused = Second != nullptr;
	if (!bReused)
	{
		Second = Extra.Pop();
		AddWarning(TEXT("The allocator did not reuse the destroyed actor's address within 64 spawns: identity reuse not exercised this run."));
	}
	AddInfo(FString::Printf(TEXT("New actor at the destroyed actor's address: %s"), bReused ? TEXT("yes") : TEXT("no")));
	FACFCameraTarget NewTarget;
	NewTarget.Actor = Second;
	Director->SetRequestLookAtTarget(Handle, NewTarget);
	Stepper.Step(Director.Get(), World);
	TestEqual(TEXT("New actor: not hidden on its first frame (the 0.25 s confirmation starts over, no state inherited)"), Director->GetLastSolverReport().NumOccluded, 0);
	for (int32 I = 0; I < 40; ++I)
	{
		Stepper.Step(Director.Get(), World);
	}
	TestEqual(TEXT("New actor: confirmed hidden after the delay"), Director->GetLastSolverReport().NumOccluded, 1);
	Wrapper.DestroyTestWorld(false);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 2 つの Director（2 人のローカルプレイヤー）: 無視リストと破棄の影響は混ざらない。正常な Collision の結果は修正前と同じ。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLifetimeIndependenceTest, "ActionCameraFramework.Lifetime.DirectorsIndependentAndCollisionUnchanged", ACFTests::Flags)
bool FACFLifetimeIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace ACFLifetimeTests;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("test world created"), Wrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	AActor* Player = SpawnPoint(World, FVector(0.0, 0.0, 200.0));
	AActor* Look = SpawnPoint(World, FVector(1000.0, 0.0, 200.0));
	AActor* WallA = SpawnBox(World, FVector(-300.0, 0.0, 200.0), FVector(20.0, 400.0, 400.0));
	AActor* WallB = SpawnBox(World, FVector(-450.0, 0.0, 200.0), FVector(20.0, 400.0, 400.0));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeCollisionPreset(false));
	TStrongObjectPtr<UACFCameraDirector> D1(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> D2(NewObject<UACFCameraDirector>());
	FACFCameraRequestParams Params;
	Params.FollowTarget.Actor = Player;
	Params.LookAtTarget.Actor = Look;
	FACFCameraRequestParams P1 = Params;
	P1.CollisionIgnoredActors.Add(WallA); // player 1 ignores wall A: stopped by wall B
	FACFCameraRequestParams P2 = Params;
	P2.CollisionIgnoredActors.Add(WallA);
	P2.CollisionIgnoredActors.Add(WallB); // player 2 ignores both: full distance
	D1->AddRequest(Preset.Get(), P1);
	D2->AddRequest(Preset.Get(), P2);
	FStepper S1;
	FStepper S2;
	uint32 Hash = 0;
	for (int32 I = 0; I < 120; ++I)
	{
		// Move the player sideways so the camera slides along the walls (deterministic collision results).
		Player->SetActorLocation(FVector(0.0, 300.0 * FMath::Sin(I / 20.0), 200.0));
		const FACFCameraEvaluationResult& R1 = S1.Step(D1.Get(), World);
		Hash = FCrc::MemCrc32(&R1.Pose.Location, sizeof(FVector), Hash);
		Hash = FCrc::MemCrc32(&R1.Pose.Rotation, sizeof(FRotator), Hash);
		const FACFCameraEvaluationResult& R2 = S2.Step(D2.Get(), World);
		Hash = FCrc::MemCrc32(&R2.Pose.Location, sizeof(FVector), Hash);
		Hash = FCrc::MemCrc32(&R2.Pose.Rotation, sizeof(FRotator), Hash);
	}
	TestTrue(FString::Printf(TEXT("Player 1 is stopped by wall B (x %.1f)"), D1->GetLastResult().Pose.Location.X), D1->GetLastResult().Pose.Location.X > -450.0 && D1->GetLastResult().Pose.Location.X < -400.0);
	TestTrue(FString::Printf(TEXT("Player 2 ignores both walls (x %.1f)"), D2->GetLastResult().Pose.Location.X), FMath::IsNearlyEqual(D2->GetLastResult().Pose.Location.X, -600.0, 1.0));
	// Recorded with the build before B-3 (Phase 6B / B-4), same scenario, Development Win64.
	constexpr uint32 Expected = 0xc843776fu;
	AddInfo(FString::Printf(TEXT("Collision pose checksum: 0x%08x"), Hash));
	TestEqual(TEXT("Normal collision results identical to the build before B-3 (bit-exact checksum)"), Hash, Expected);

	// Player 2's ignored wall B is destroyed: player 1 (who collides with it) and player 2 both go on correctly.
	WallB->Destroy();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	for (int32 I = 0; I < 180; ++I) // the collision returns to full distance with its own (smoothed) return time
	{
		S1.Step(D1.Get(), World);
		S2.Step(D2.Get(), World);
	}
	TestTrue(TEXT("Wall B gone: player 1 now reaches full distance"), FMath::IsNearlyEqual(D1->GetLastResult().Pose.Location.X, -600.0, 1.0));
	TestTrue(TEXT("Wall B gone: player 2 unchanged"), FMath::IsNearlyEqual(D2->GetLastResult().Pose.Location.X, -600.0, 1.0));
	Wrapper.DestroyTestWorld(false);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Phase 6B / B-5: 演出を載せられる ACF カメラがあるか（要求 / フェードアウト中 / カットで解除）と、World が変わると演出が終わること。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLifetimeEffectsHostTest, "ActionCameraFramework.Lifetime.EffectsNeedAnAcfCameraAndEndWithTheWorld", ACFTests::Flags)
bool FACFLifetimeEffectsHostTest::RunTest(const FString& Parameters)
{
	using namespace ACFLifetimeTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Fading(MakeFixedPreset(FVector(0.0, 0.0, 300.0), 80.f, Linear(0.5f)));
	TStrongObjectPtr<UACFCameraPreset> Cut(MakeFixedPreset(FVector(0.0, 0.0, 300.0), 80.f));
	TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	UACFCameraEffect_Punch* Punch = NewObject<UACFCameraEffect_Punch>(Effect.Get());
	Punch->Envelope.Attack = 0.f;
	Punch->Envelope.Sustain = -1.f; // infinite: only a stop ends it
	Punch->Envelope.Release = 0.f;
	Effect->Layers.Add(Punch);
	FStepper Stepper;
	TestFalse(TEXT("No request ever: no ACF camera to host effects"), Director->CanHostEffects());

	FACFCameraRequestHandle Handle = Director->AddRequest(Fading.Get(), FACFCameraRequestParams());
	TestTrue(TEXT("Request added (not evaluated yet): effects can play"), Director->CanHostEffects());
	Stepper.Step(Director.Get(), nullptr);
	Director->RemoveRequest(Handle, nullptr, false);
	TestTrue(TEXT("Released with a 0.5 s blend out: the ACF camera stays on screen meanwhile"), Director->CanHostEffects());
	for (int32 I = 0; I < 10; ++I)
	{
		Stepper.Step(Director.Get(), nullptr);
	}
	TestTrue(TEXT("Blending out: still on screen"), Director->CanHostEffects());
	for (int32 I = 0; I < 40; ++I)
	{
		Stepper.Step(Director.Get(), nullptr);
	}
	TestFalse(TEXT("Blend out finished: nothing left to host effects"), Director->CanHostEffects());

	Handle = Director->AddRequest(Cut.Get(), FACFCameraRequestParams());
	Stepper.Step(Director.Get(), nullptr);
	Director->RemoveRequest(Handle, nullptr, false);
	TestFalse(TEXT("Released with a cut: no ACF camera from the next frame, effects rejected right away"), Director->CanHostEffects());
	Stepper.Step(Director.Get(), nullptr);
	TestFalse(TEXT("After the cut"), Director->CanHostEffects());

	// Effects are stopped when the camera moves to another World (map travel): nothing carries over.
	UWorld* WorldA = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		WorldA = WorldA ? WorldA : WorldContext.World();
	}
	Handle = Director->AddRequest(Cut.Get(), FACFCameraRequestParams());
	Stepper.Step(Director.Get(), WorldA);
	const FACFCameraEffectHandle Infinite = Director->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	for (int32 I = 0; I < 5; ++I)
	{
		Stepper.Step(Director.Get(), WorldA);
	}
	TestTrue(TEXT("Same world: the infinite effect keeps running"), Director->IsEffectActive(Infinite));
	Stepper.Step(Director.Get(), nullptr); // another world
	TestFalse(TEXT("Another world: the effect of the previous world was stopped"), Director->IsEffectActive(Infinite));
	TestEqual(TEXT("Another world: no effect left"), Director->GetNumActiveEffects(), 0);
	TestTrue(TEXT("Another world: the request itself is kept (it belongs to the local player)"), Director->HasRequest(Handle));
	const FACFCameraEffectHandle Fresh = Director->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	Stepper.Step(Director.Get(), nullptr);
	TestTrue(TEXT("New world: a new effect plays"), Director->IsEffectActive(Fresh));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
