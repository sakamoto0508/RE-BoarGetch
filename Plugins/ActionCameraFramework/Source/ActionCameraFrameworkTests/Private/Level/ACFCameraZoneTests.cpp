#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestEnvironment.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneResolver.h"
#include "ACFCameraZoneSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Math/RandomStream.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7C-1A: Camera Zone selection (README 32 章). Resolver rules and geometry as pure functions; the zone subsystem in
 * the real game World (registration, transforms, players, enable / disable, destruction, level removal, the per-frame
 * driver). No camera request is made in 7C-1A.
 */
namespace ACFZoneTests
{
	constexpr EAutomationTestFlags PureFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	constexpr EAutomationTestFlags WorldFlags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	inline FACFCameraZoneCandidate Make(const TCHAR* Key, int32 Priority, double Volume, bool bInside = true, bool bCurrent = false, bool bEnabled = true, bool bInsideWithMargin = false)
	{
		FACFCameraZoneCandidate C;
		C.StableKey = Key;
		C.Priority = Priority;
		C.Volume = Volume;
		C.bInside = bInside;
		C.bCurrent = bCurrent;
		C.bEnabled = bEnabled;
		C.bInsideWithMargin = bInside || bInsideWithMargin;
		return C;
	}

	inline FString Winner(TConstArrayView<FACFCameraZoneCandidate> Candidates)
	{
		const int32 Index = ACFCameraZoneResolver::Resolve(Candidates);
		return Index == INDEX_NONE ? FString(TEXT("<none>")) : Candidates[Index].StableKey;
	}

	/** A box zone for the pure simulation: transform, extent, priority, margin. */
	struct FZoneBox
	{
		FString Key;
		FTransform Transform;
		FVector Extent;
		int32 Priority = 0;
		double Margin = 50.0;
		bool bEnabled = true;
	};

	/** Resolves Point against Boxes the way the zone subsystem does, with Current as the previous selection. */
	inline FString Step(const TArray<FZoneBox>& Boxes, const FVector& Point, FString& Current)
	{
		TArray<FACFCameraZoneCandidate> Candidates;
		for (const FZoneBox& Box : Boxes)
		{
			FACFCameraZoneCandidate C;
			C.StableKey = Box.Key;
			C.Priority = Box.Priority;
			C.bEnabled = Box.bEnabled;
			C.bCurrent = Box.Key == Current;
			C.bInside = ACFCameraZoneGeometry::BoxContains(Box.Transform, Box.Extent, Point, 0.0);
			C.bInsideWithMargin = ACFCameraZoneGeometry::BoxContains(Box.Transform, Box.Extent, Point, Box.Margin);
			C.Volume = ACFCameraZoneGeometry::BoxVolume(Box.Transform, Box.Extent);
			Candidates.Add(C);
		}
		const int32 Index = ACFCameraZoneResolver::Resolve(Candidates);
		Current = Index == INDEX_NONE ? FString() : Candidates[Index].StableKey;
		return Current;
	}
}

// -------------------------------------------------------------------------------------------------
// Resolver: priority, then the current zone, then the smaller volume, then the stable key; independent of the order.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneResolverRulesTest, "ActionCameraFramework.Zone.Resolver.PriorityAndTieBreaks", ACFZoneTests::PureFlags)
bool FACFZoneResolverRulesTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneTests;
	TestEqual(TEXT("No candidate: none"), Winner({}), FString(TEXT("<none>")));
	TestEqual(TEXT("Outside every zone: none"), Winner({ Make(TEXT("A"), 0, 10.0, false), Make(TEXT("B"), 3, 5.0, false) }), FString(TEXT("<none>")));
	TestEqual(TEXT("Higher priority wins over the current zone and the smaller volume"),
		Winner({ Make(TEXT("Small"), 0, 1.0, true, true), Make(TEXT("High"), 2, 1000.0) }), FString(TEXT("High")));
	TestEqual(TEXT("Equal priority: the current zone stays (even if larger)"),
		Winner({ Make(TEXT("Small"), 1, 1.0), Make(TEXT("Current"), 1, 1000.0, true, true) }), FString(TEXT("Current")));
	TestEqual(TEXT("Equal priority, no current: the smaller volume"),
		Winner({ Make(TEXT("A_Large"), 1, 1000.0), Make(TEXT("Z_Small"), 1, 10.0) }), FString(TEXT("Z_Small")));
	TestEqual(TEXT("Equal priority and volume (within tolerance): the smaller stable key"),
		Winner({ Make(TEXT("Zone_B"), 1, 1000.0), Make(TEXT("Zone_A"), 1, 1000.0 * (1.0 + 1.0e-9)) }), FString(TEXT("Zone_A")));
	TestEqual(TEXT("A disabled zone is not selected (even the current one, even inside)"),
		Winner({ Make(TEXT("Disabled"), 9, 1.0, true, true, false), Make(TEXT("Other"), 0, 10.0) }), FString(TEXT("Other")));
	TestEqual(TEXT("Margin: only the current zone uses it"),
		Winner({ Make(TEXT("NotCurrent"), 5, 1.0, false, false, true, true), Make(TEXT("Current"), 0, 10.0, false, true, true, true) }), FString(TEXT("Current")));
	TestEqual(TEXT("Margin: a non-current zone within its margin is not entered"),
		Winner({ Make(TEXT("NotCurrent"), 5, 1.0, false, false, true, true) }), FString(TEXT("<none>")));

	// Strict order: irreflexive and antisymmetric over a mixed set.
	const TArray<FACFCameraZoneCandidate> Set = {
		Make(TEXT("A"), 0, 50.0), Make(TEXT("B"), 1, 500.0), Make(TEXT("C"), 1, 100.0), Make(TEXT("D"), 1, 100.0), Make(TEXT("E"), 1, 900.0, true, true), Make(TEXT("F"), -3, 1.0) };
	bool bStrict = true;
	for (const FACFCameraZoneCandidate& X : Set)
	{
		bStrict &= !ACFCameraZoneResolver::Beats(X, X);
		for (const FACFCameraZoneCandidate& Y : Set)
		{
			bStrict &= !(ACFCameraZoneResolver::Beats(X, Y) && ACFCameraZoneResolver::Beats(Y, X));
			bStrict &= &X == &Y || ACFCameraZoneResolver::Beats(X, Y) || ACFCameraZoneResolver::Beats(Y, X); // total
		}
	}
	TestTrue(TEXT("Beats is a strict total order on distinct zones"), bStrict);
	TestEqual(TEXT("Mixed set: the current zone among the highest priority"), Winner(Set), FString(TEXT("E")));

	// The result does not depend on the registration order (200 shuffles of the set, with and without a current zone).
	for (const bool bWithCurrent : { true, false })
	{
		TArray<FACFCameraZoneCandidate> Candidates = Set;
		Candidates[4].bCurrent = bWithCurrent;
		const FString Expected = bWithCurrent ? FString(TEXT("E")) : FString(TEXT("C"));
		FRandomStream Random(1234);
		bool bSame = true;
		for (int32 Shuffle = 0; Shuffle < 200; ++Shuffle)
		{
			for (int32 I = Candidates.Num() - 1; I > 0; --I)
			{
				Candidates.Swap(I, Random.RandRange(0, I));
			}
			bSame &= Winner(Candidates) == Expected;
		}
		TestTrue(FString::Printf(TEXT("Order independent (%s current): always %s"), bWithCurrent ? TEXT("with") : TEXT("without"), *Expected), bSame);
	}
	return true;
}

// Spatial hysteresis along a path: two adjacent zones of equal priority, a nested higher one, jitter on the boundary.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneResolverMarginTest, "ActionCameraFramework.Zone.Resolver.ExitMarginHysteresis", ACFZoneTests::PureFlags)
bool FACFZoneResolverMarginTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneTests;
	TArray<FZoneBox> Boxes;
	Boxes.Add({ TEXT("A"), FTransform(FVector(-250.0, 0.0, 0.0)), FVector(250.0, 250.0, 250.0), 0, 50.0 }); // x in [-500, 0]
	Boxes.Add({ TEXT("B"), FTransform(FVector(250.0, 0.0, 0.0)), FVector(250.0, 250.0, 250.0), 0, 50.0 });  // x in [0, 500]
	Boxes.Add({ TEXT("C"), FTransform(FVector(150.0, 0.0, 0.0)), FVector(50.0, 50.0, 50.0), 1, 50.0 });     // x in [100, 200]
	FString Current;
	TestEqual(TEXT("Outside by 20 cm (within A's margin) without a current zone: not entered"), Step(Boxes, FVector(-520.0, 0.0, 0.0), Current), FString());
	TestEqual(TEXT("Inside A"), Step(Boxes, FVector(-300.0, 0.0, 0.0), Current), FString(TEXT("A")));
	TestEqual(TEXT("10 cm into B, within A's margin: stays A"), Step(Boxes, FVector(10.0, 0.0, 0.0), Current), FString(TEXT("A")));
	TestEqual(TEXT("60 cm into B (beyond A's margin): B"), Step(Boxes, FVector(60.0, 0.0, 0.0), Current), FString(TEXT("B")));
	int32 Switches = 0;
	FString Last = Current;
	for (int32 I = 0; I < 200; ++I)
	{
		const double X = 40.0 * FMath::Sin(I * 0.7); // jitter of +-40 cm around the boundary (inside the 50 cm margin)
		Switches += Step(Boxes, FVector(X, 0.0, 0.0), Current) != Last ? 1 : 0;
		Last = Current;
	}
	TestEqual(TEXT("Jitter of +-40 cm on the boundary: no switch"), Switches, 0);
	TestEqual(TEXT("Higher priority nested zone: entered at its boundary"), Step(Boxes, FVector(101.0, 0.0, 0.0), Current), FString(TEXT("C")));
	TestEqual(TEXT("Leaving C by 20 cm: kept (margin)"), Step(Boxes, FVector(220.0, 0.0, 0.0), Current), FString(TEXT("C")));
	TestEqual(TEXT("Leaving C by 60 cm: back to B"), Step(Boxes, FVector(260.0, 0.0, 0.0), Current), FString(TEXT("B")));
	Boxes[1].bEnabled = false;
	TestEqual(TEXT("B disabled while inside: left at once (no margin)"), Step(Boxes, FVector(260.0, 0.0, 0.0), Current), FString());
	return true;
}

// Point-in-shape tests with rotation, non-uniform / negative / zero scale, margins in world units, volumes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneGeometryTest, "ActionCameraFramework.Zone.Geometry.BoxAndSphere", ACFZoneTests::PureFlags)
bool FACFZoneGeometryTest::RunTest(const FString& Parameters)
{
	using namespace ACFCameraZoneGeometry;
	// Extent (100, 50, 25) scaled x2 on local X, rotated 90 deg (local X = world Y), moved to (1000, 0, 0).
	const FTransform Transform(FRotator(0.0, 90.0, 0.0), FVector(1000.0, 0.0, 0.0), FVector(2.0, 1.0, 1.0));
	const FVector Extent(100.0, 50.0, 25.0);
	TestTrue(TEXT("Box: 190 along local X (scaled extent 200)"), BoxContains(Transform, Extent, FVector(1000.0, 190.0, 0.0), 0.0));
	TestFalse(TEXT("Box: 210 along local X"), BoxContains(Transform, Extent, FVector(1000.0, 210.0, 0.0), 0.0));
	TestTrue(TEXT("Box: 210 with a 20 cm margin (world units)"), BoxContains(Transform, Extent, FVector(1000.0, 210.0, 0.0), 20.0));
	TestFalse(TEXT("Box: 90 along world X is local Y (extent 50)"), BoxContains(Transform, Extent, FVector(1090.0, 0.0, 0.0), 0.0));
	TestTrue(TEXT("Box: 45 along world X"), BoxContains(Transform, Extent, FVector(1045.0, 0.0, 0.0), 0.0));
	TestTrue(TEXT("Box: 40 cm margin on the short axis too (world cm, not scaled)"), BoxContains(Transform, Extent, FVector(1089.0, 0.0, 0.0), 40.0));
	TestFalse(TEXT("Box: the untranslated origin is outside"), BoxContains(Transform, Extent, FVector::ZeroVector, 0.0));
	const FTransform Mirrored(FRotator(0.0, 90.0, 0.0), FVector(1000.0, 0.0, 0.0), FVector(-2.0, 1.0, 1.0));
	TestTrue(TEXT("Box: negative scale counts as positive"), BoxContains(Mirrored, Extent, FVector(1000.0, -190.0, 0.0), 0.0));
	const FTransform Flat(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1.0, 0.0, 1.0));
	TestFalse(TEXT("Box: zero scale contains nothing (even its center)"), BoxContains(Flat, Extent, FVector::ZeroVector, 10.0));
	TestEqual(TEXT("Box volume: 400 x 100 x 50"), BoxVolume(Transform, Extent), 2000000.0);
	TestEqual(TEXT("Box volume: zero scale"), BoxVolume(Flat, Extent), 0.0);
	TestTrue(TEXT("Sphere: 99 of 100"), SphereContains(FVector(0.0, 0.0, 500.0), 100.0, FVector(0.0, 99.0, 500.0), 0.0));
	TestFalse(TEXT("Sphere: 101 of 100"), SphereContains(FVector(0.0, 0.0, 500.0), 100.0, FVector(0.0, 101.0, 500.0), 0.0));
	TestTrue(TEXT("Sphere: 104 with a 5 cm margin"), SphereContains(FVector(0.0, 0.0, 500.0), 100.0, FVector(0.0, 104.0, 500.0), 5.0));
	TestFalse(TEXT("Sphere: radius 0 contains nothing"), SphereContains(FVector::ZeroVector, 0.0, FVector::ZeroVector, 10.0));
	TestTrue(TEXT("Sphere volume"), FMath::IsNearlyEqual(SphereVolume(10.0), 4.0 / 3.0 * UE_DOUBLE_PI * 1000.0, 1e-6));
	TestFalse(TEXT("Negative margin is no margin (not a smaller shape, not a larger one)"), SphereContains(FVector::ZeroVector, 100.0, FVector(100.5, 0.0, 0.0), -10.0));
	TestTrue(TEXT("Negative margin does not shrink"), SphereContains(FVector::ZeroVector, 100.0, FVector(99.5, 0.0, 0.0), -10.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The zone subsystem in the real game World (synchronous: UpdatePlayer is called directly with chosen locations).

namespace ACFZoneTests
{
	struct FWorldZones
	{
		UWorld* World = nullptr;
		UACFCameraZoneSubsystem* Subsystem = nullptr;
		TArray<TWeakObjectPtr<AActor>> Spawned;

		bool Init(FAutomationTestBase& Test)
		{
			World = ACFTestEnvironment::FindGameWorld();
			Subsystem = World ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
			if (!World || !World->HasBegunPlay() || !Subsystem)
			{
				Test.AddWarning(TEXT("Skipped: needs a game World that has begun play (run with -game)."));
				return false;
			}
			return true;
		}

		AACFCameraZone* Box(const FVector& Location, const FVector& Extent, int32 Priority, const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Scale = FVector::OneVector)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AACFCameraZone* Zone = World->SpawnActor<AACFCameraZone>(Location, Rotation, Params);
			Zone->SetActorScale3D(Scale);
			Zone->GetBox()->SetBoxExtent(Extent, false);
			Zone->GetZone()->ZonePriority = Priority;
			Spawned.Add(Zone);
			return Zone;
		}

		/** A plain actor with a sphere and a zone component added at run time (the component on a custom actor). */
		UACFCameraZoneComponent* Sphere(const FVector& Location, float Radius, int32 Priority, const FVector& Scale = FVector::OneVector)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator::ZeroRotator, Location, Scale), Params);
			USphereComponent* Shape = NewObject<USphereComponent>(Actor, TEXT("Shape"));
			Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Shape->SetSphereRadius(Radius, false);
			Actor->SetRootComponent(Shape);
			Shape->RegisterComponent();
			Actor->SetActorTransform(FTransform(FRotator::ZeroRotator, Location, Scale));
			UACFCameraZoneComponent* Zone = NewObject<UACFCameraZoneComponent>(Actor, TEXT("Zone"));
			Zone->ZonePriority = Priority;
			Zone->RegisterComponent(); // the actor has begun play: the component begins play (and registers) now
			Spawned.Add(Actor);
			return Zone;
		}

		void Cleanup()
		{
			for (const TWeakObjectPtr<AActor>& Actor : Spawned)
			{
				if (Actor.IsValid() && !Actor->IsActorBeingDestroyed())
				{
					Actor->Destroy();
				}
			}
			Spawned.Reset();
		}
	};

	/** Far from the test maps' geometry and players. */
	const FVector Far(0.0, 0.0, -200000.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneWorldTest, "ActionCameraFramework.Zone.World.RegistrationPlayersAndLifetime", ACFZoneTests::WorldFlags)
bool FACFZoneWorldTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneTests;
	FWorldZones W;
	if (!W.Init(*this))
	{
		return true;
	}
	AddExpectedMessagePlain(TEXT("has no Camera Preset"), EAutomationExpectedMessageFlags::Contains, -1); // 7C-1B: selection-only zones
	ULocalPlayer* Player1 = W.World->GetGameInstance() ? W.World->GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!TestNotNull(TEXT("First local player"), Player1))
	{
		return false;
	}
	// A second key: the subsystem keys its state by local player only (the frame driver passes each player's pawn location).
	TStrongObjectPtr<ULocalPlayer> Player2(NewObject<ULocalPlayer>(GEngine)); // ULocalPlayer is Within=Engine
	const int32 ZonesBefore = W.Subsystem->GetNumZones();
	TArray<FString> Events;
	const FDelegateHandle Handle = W.Subsystem->OnZoneSelectionChanged.AddLambda([&Events](ULocalPlayer* Player, UACFCameraZoneComponent* From, UACFCameraZoneComponent* To)
	{
		Events.Add(FString::Printf(TEXT("%s:%s>%s"), *GetNameSafe(Player), From ? *GetNameSafe(From->GetOwner()) : TEXT("-"), To ? *GetNameSafe(To->GetOwner()) : TEXT("-")));
	});

	// Big A (priority 0), B inside it (priority 1), rotated and scaled C (priority 0) elsewhere, a sphere zone with a scale.
	AACFCameraZone* A = W.Box(Far, FVector(1000.0, 1000.0, 500.0), 0);
	AACFCameraZone* B = W.Box(Far + FVector(500.0, 0.0, 0.0), FVector(200.0, 200.0, 200.0), 1);
	AACFCameraZone* C = W.Box(Far + FVector(5000.0, 0.0, 0.0), FVector(100.0, 50.0, 50.0), 0, FRotator(0.0, 90.0, 0.0), FVector(2.0, 1.0, 1.0));
	UACFCameraZoneComponent* S = W.Sphere(Far + FVector(-5000.0, 0.0, 0.0), 100.f, 0, FVector(2.0, 3.0, 4.0));
	TestEqual(TEXT("Four zones registered at BeginPlay"), W.Subsystem->GetNumZones(), ZonesBefore + 4);

	FVector InB = Far + FVector(500.0, 0.0, 0.0);
	FVector InAOnly = Far + FVector(-500.0, 0.0, 0.0);
	TestTrue(TEXT("Player 1 in B (inside A too): B by priority"), W.Subsystem->UpdatePlayer(Player1, &InB) == B->GetZone());
	TestTrue(TEXT("Player 2 in A only: A (independent of player 1)"), W.Subsystem->UpdatePlayer(Player2.Get(), &InAOnly) == A->GetZone());
	TestTrue(TEXT("Player 1 still B"), W.Subsystem->GetSelectedZone(Player1) == B->GetZone());

	// Transforms of real components: C is rotated 90 deg and scaled x2 on local X (= world Y, half size 200).
	FVector InC = Far + FVector(5000.0, 190.0, 0.0);
	FVector OutC = Far + FVector(5000.0, 0.0, 0.0) + FVector(120.0, 0.0, 0.0);
	TestTrue(TEXT("Rotated / scaled box: 190 along world Y is inside"), W.Subsystem->UpdatePlayer(Player2.Get(), &InC) == C->GetZone());
	TestTrue(TEXT("Rotated / scaled box: 120 along world X (local Y, half size 50) is outside, beyond the 50 cm margin too"), W.Subsystem->UpdatePlayer(Player2.Get(), &OutC) == nullptr);
	// The sphere: radius 100 x the smallest scale (2) = 200, as USphereComponent.
	FVector InS = Far + FVector(-5000.0, 195.0, 0.0);
	FVector OutS = Far + FVector(-5000.0, 0.0, 260.0);
	TestTrue(TEXT("Scaled sphere: 195 inside (radius 200)"), W.Subsystem->UpdatePlayer(Player2.Get(), &InS) == S);
	TestTrue(TEXT("Scaled sphere: 260 outside (beyond the margin)"), W.Subsystem->UpdatePlayer(Player2.Get(), &OutS) == nullptr);
	TestTrue(TEXT("No pawn (no location): nothing"), W.Subsystem->UpdatePlayer(Player2.Get(), nullptr) == nullptr);
	W.Subsystem->UpdatePlayer(Player2.Get(), &InAOnly);

	// Disable B: player 1 leaves it at once (no update call), to A. Enable again: back to B at once.
	Events.Reset();
	B->GetZone()->SetZoneEnabled(false);
	TestTrue(TEXT("B disabled: player 1 is in A at once"), W.Subsystem->GetSelectedZone(Player1) == A->GetZone());
	B->GetZone()->SetZoneEnabled(true);
	TestTrue(TEXT("B enabled: player 1 is in B at once"), W.Subsystem->GetSelectedZone(Player1) == B->GetZone());
	TestTrue(TEXT("Player 2 unaffected (A)"), W.Subsystem->GetSelectedZone(Player2.Get()) == A->GetZone());
	TestEqual(FString::Printf(TEXT("Two change events for player 1 (%s)"), *FString::Join(Events, TEXT(", "))), Events.Num(), 2);

	// Destroy B (EndPlay Destroyed): player 1 falls back to A at once.
	B->Destroy();
	TestTrue(TEXT("B destroyed: player 1 in A at once"), W.Subsystem->GetSelectedZone(Player1) == A->GetZone());
	TestEqual(TEXT("B unregistered"), W.Subsystem->GetNumZones(), ZonesBefore + 3);
	// A removed with its level (EndPlay RemovedFromWorld, what level streaming does): both players select nothing at once.
	A->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestTrue(TEXT("A removed from the World: player 1 has no zone"), W.Subsystem->GetSelectedZone(Player1) == nullptr);
	TestTrue(TEXT("A removed from the World: player 2 has no zone"), W.Subsystem->GetSelectedZone(Player2.Get()) == nullptr);
	TestEqual(TEXT("A unregistered"), W.Subsystem->GetNumZones(), ZonesBefore + 2);

	// Two zones with the same priority and the same volume at the same place: the smaller path name, in any order.
	AACFCameraZone* T1 = W.Box(Far + FVector(0.0, 9000.0, 0.0), FVector(300.0, 300.0, 300.0), 4);
	AACFCameraZone* T2 = W.Box(Far + FVector(0.0, 9000.0, 0.0), FVector(300.0, 300.0, 300.0), 4);
	FVector InT = Far + FVector(0.0, 9000.0, 0.0);
	W.Subsystem->UpdatePlayer(Player2.Get(), nullptr);
	UACFCameraZoneComponent* Expected = T1->GetZone()->GetStableKey() < T2->GetZone()->GetStableKey() ? T1->GetZone() : T2->GetZone();
	TestTrue(TEXT("Exact tie: the smaller stable key"), W.Subsystem->UpdatePlayer(Player2.Get(), &InT) == Expected);

	// A removed local player is dropped from the states.
	const int32 StatesBefore = W.Subsystem->GetNumPlayerStates();
	Player2.Reset();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	W.Subsystem->UpdatePlayer(Player1, &InAOnly);
	TestEqual(TEXT("The second key's state is dropped once it is gone"), W.Subsystem->GetNumPlayerStates(), StatesBefore - 1);

	W.Subsystem->OnZoneSelectionChanged.Remove(Handle);
	W.Cleanup();
	TestEqual(TEXT("Every test zone unregistered"), W.Subsystem->GetNumZones(), ZonesBefore);
	return true;
}

// The frame driver: the zone subsystem ticks once per frame (after the actors, before the camera; also without any ACF
// request: the test makes none) and resolves the real
// player's pawn location.
namespace ACFZoneTests
{
	struct FDriverFlow
	{
		FAutomationTestBase* Test = nullptr;
		FWorldZones W;
		TWeakObjectPtr<AACFCameraZone> A;
		TWeakObjectPtr<AACFCameraZone> B;
		uint64 Mark = 0;
	};
	using FDriverStep = TFunction<bool(FDriverFlow&)>;

	class FDriverCommand : public IAutomationLatentCommand
	{
	public:
		FDriverCommand(TSharedRef<FDriverFlow> InFlow, TArray<FDriverStep> InSteps) : Flow(InFlow), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			if (++Frames > 600)
			{
				Flow->Test->AddError(FString::Printf(TEXT("Zone driver flow: step %d timed out."), Index));
				Flow->W.Cleanup();
				return true;
			}
			if (Index < Steps.Num() && Steps[Index](*Flow))
			{
				++Index;
			}
			return Index >= Steps.Num();
		}
	private:
		TSharedRef<FDriverFlow> Flow;
		TArray<FDriverStep> Steps;
		int32 Index = 0;
		int32 Frames = 0;
	};

	inline ULocalPlayer* FirstPlayer(UWorld* World) { return World && World->GetGameInstance() ? World->GetGameInstance()->GetFirstGamePlayer() : nullptr; }
	inline APawn* FirstPawn(UWorld* World) { const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr; return PC ? PC->GetPawn() : nullptr; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneDriverTest, "ActionCameraFramework.Zone.World.FrameDriverFollowsThePawn", ACFZoneTests::WorldFlags)
bool FACFZoneDriverTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneTests;
	TSharedRef<FDriverFlow> Flow = MakeShared<FDriverFlow>();
	Flow->Test = this;
	AddExpectedMessagePlain(TEXT("has no Camera Preset"), EAutomationExpectedMessageFlags::Contains, -1); // 7C-1B: selection-only zones
	if (!Flow->W.Init(*this))
	{
		return true;
	}
	if (!FirstPawn(Flow->W.World) || !FirstPlayer(Flow->W.World))
	{
		AddWarning(TEXT("Skipped: needs the first player's pawn."));
		return true;
	}
	TArray<FDriverStep> Steps;
	Steps.Add([](FDriverFlow& F)
	{
		// A around the pawn (the pawn does not move: the zones do).
		F.A = F.W.Box(FirstPawn(F.W.World)->GetActorLocation(), FVector(300.0, 300.0, 300.0), 0);
		F.Mark = GFrameCounter;
		return true;
	});
	Steps.Add([](FDriverFlow& F) { return GFrameCounter > F.Mark + 2; });
	Steps.Add([](FDriverFlow& F)
	{
		F.Test->TestTrue(TEXT("Driver: the pawn's zone is selected without any call"), F.W.Subsystem->GetSelectedZone(FirstPlayer(F.W.World)) == F.A->GetZone());
		// B, higher priority, also around the pawn.
		F.B = F.W.Box(FirstPawn(F.W.World)->GetActorLocation(), FVector(100.0, 100.0, 300.0), 2);
		F.Mark = GFrameCounter;
		return true;
	});
	Steps.Add([](FDriverFlow& F) { return GFrameCounter > F.Mark + 2; });
	Steps.Add([](FDriverFlow& F)
	{
		F.Test->TestTrue(TEXT("Driver: the higher priority zone"), F.W.Subsystem->GetSelectedZone(FirstPlayer(F.W.World)) == F.B->GetZone());
		// Move B 30 cm past the pawn's side (within its margin): kept. Then far away: back to A.
		const FVector Pawn = FirstPawn(F.W.World)->GetActorLocation();
		F.B->SetActorLocation(Pawn + FVector(130.0, 0.0, 0.0));
		F.Mark = GFrameCounter;
		return true;
	});
	Steps.Add([](FDriverFlow& F) { return GFrameCounter > F.Mark + 2; });
	Steps.Add([](FDriverFlow& F)
	{
		F.Test->TestTrue(TEXT("Driver: 30 cm outside B (margin 50): B kept"), F.W.Subsystem->GetSelectedZone(FirstPlayer(F.W.World)) == F.B->GetZone());
		F.B->SetActorLocation(FirstPawn(F.W.World)->GetActorLocation() + FVector(1000.0, 0.0, 0.0));
		F.Mark = GFrameCounter;
		return true;
	});
	Steps.Add([](FDriverFlow& F) { return GFrameCounter > F.Mark + 2; });
	Steps.Add([](FDriverFlow& F)
	{
		F.Test->TestTrue(TEXT("Driver: B moved away: back to A"), F.W.Subsystem->GetSelectedZone(FirstPlayer(F.W.World)) == F.A->GetZone());
		F.A->Destroy();
		F.Test->TestTrue(TEXT("Driver: A destroyed: no zone at once"), F.W.Subsystem->GetSelectedZone(FirstPlayer(F.W.World)) == nullptr);
		F.Mark = GFrameCounter;
		return true;
	});
	Steps.Add([](FDriverFlow& F) { return GFrameCounter > F.Mark + 2; });
	Steps.Add([](FDriverFlow& F)
	{
		F.Test->TestTrue(TEXT("Driver: still no zone on the next frames"), F.W.Subsystem->GetSelectedZone(FirstPlayer(F.W.World)) == nullptr);
		F.W.Cleanup();
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FDriverCommand(Flow, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
