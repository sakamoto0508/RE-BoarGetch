#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestEnvironment.h"
#include "Core/ACFCameraTestUtils.h"
#include "ACFCameraTuning.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneSettings.h"
#include "ACFCameraZoneSubsystem.h"
#include "Components/BoxComponent.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7C-2A: Enter / Exit Delay (the Pending Candidate), teleports and pawn changes (README 32 章).
 * The synchronous tests call UpdatePlayer with chosen locations, game time and pawns in the real game World (no frame runs
 * in between); the first player's real camera counts the zone requests. The latent tests move the real pawn.
 */
namespace ACFZoneDwellTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	const FVector Far(0.0, 0.0, -300000.0);

	struct FSync
	{
		UWorld* World = nullptr;
		UACFCameraZoneSubsystem* Zones = nullptr;
		ULocalPlayer* Player = nullptr;
		UACFCameraSubsystem* Acf = nullptr;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TArray<TStrongObjectPtr<UObject>> Keep;
		int32 RequestsAtStart = 0;
		/** Distinct zone request handles seen (each one is a request the zone subsystem made). */
		TArray<FACFCameraRequestHandle> Seen;

		bool Init(FAutomationTestBase& Test)
		{
			World = ACFTestEnvironment::FindGameWorld();
			Zones = World ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
			Player = World && World->GetGameInstance() ? World->GetGameInstance()->GetFirstGamePlayer() : nullptr;
			Acf = Player ? Player->GetSubsystem<UACFCameraSubsystem>() : nullptr;
			if (!World || !World->HasBegunPlay() || !Zones || !Acf)
			{
				Test.AddWarning(TEXT("Skipped: needs a game World with the first local player (run with -game)."));
				return false;
			}
			RequestsAtStart = Acf->GetDirector()->GetNumRequests();
			return true;
		}

		AACFCameraZone* Box(const FVector& Center, const FVector& Extent, int32 Priority, float EnterDelay, float ExitDelay, double Margin = 50.0)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AACFCameraZone* Zone = World->SpawnActor<AACFCameraZone>(Center, FRotator::ZeroRotator, Params);
			Zone->GetBox()->SetBoxExtent(Extent, false);
			UACFCameraZoneComponent* Component = Zone->GetZone();
			Component->ZonePriority = Priority;
			Component->EnterDelay = EnterDelay;
			Component->ExitDelay = ExitDelay;
			Component->ExitMargin = Margin;
			UACFCameraPreset* Preset = ACFTests::MakeFixedPreset(Center + FVector(0.0, 0.0, 500.0), 70.f);
			Keep.Emplace(Preset);
			Component->Preset = Preset;
			Spawned.Add(Zone);
			return Zone;
		}

		AActor* Dummy(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			Spawned.Add(Actor);
			return Actor;
		}

		/** One update at X along the zones' axis (Far + (X, 0, 0)). */
		UACFCameraZoneComponent* At(const FVector& Location, float Dt, const AActor* Pawn = nullptr, bool bTeleport = false, ULocalPlayer* Who = nullptr)
		{
			FACFCameraZonePlayerUpdate Update;
			Update.Location = &Location;
			Update.DeltaTime = Dt;
			Update.Pawn = Pawn;
			Update.bTeleport = bTeleport;
			UACFCameraZoneComponent* Selected = Zones->UpdatePlayer(Who ? Who : Player, Update);
			const FACFCameraRequestHandle Handle = Zones->GetZoneRequest(Player);
			if (Handle.IsValid() && !Seen.Contains(Handle))
			{
				Seen.Add(Handle);
			}
			return Selected;
		}

		UACFCameraZoneComponent* Selected(ULocalPlayer* Who = nullptr) const { return Zones->GetSelectedZone(Who ? Who : Player); }

		bool Pending(UACFCameraZoneComponent*& Zone, float& Elapsed, ULocalPlayer* Who = nullptr) const { return Zones->GetPendingZone(Who ? Who : Player, Zone, Elapsed); }

		/** Leaves every zone (no pawn) and removes the test zones; the player's requests are as before the test. */
		void Cleanup(FAutomationTestBase& Test)
		{
			Zones->UpdatePlayer(Player, static_cast<const FVector*>(nullptr));
			for (const TWeakObjectPtr<AActor>& Actor : Spawned)
			{
				if (Actor.IsValid() && !Actor->IsActorBeingDestroyed())
				{
					Actor->Destroy();
				}
			}
			Spawned.Reset();
			Test.TestEqual(TEXT("Cleanup: the player's requests as before the test"), Acf->GetDirector()->GetNumRequests(), RequestsAtStart);
		}
	};
}

// The boundary of two zones (Enter / Exit Delay 0.3 s, Exit Margin 50 cm) crossed back and forth faster than the delay at
// 30 / 60 / 120 fps: no selection change and no request; holding on one side switches after the delay at every frame rate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneDwellFrameRateTest, "ActionCameraFramework.Zone.Dwell.BoundaryJitterAtFrameRates", ACFZoneDwellTests::Flags)
bool FACFZoneDwellFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneDwellTests;
	FSync S;
	if (!S.Init(*this))
	{
		return true;
	}
	AACFCameraZone* A = S.Box(Far + FVector(-500.0, 0.0, 0.0), FVector(500.0, 500.0, 500.0), 0, 0.3f, 0.3f); // x in [-1000, 0]
	AACFCameraZone* B = S.Box(Far + FVector(500.0, 0.0, 0.0), FVector(500.0, 500.0, 500.0), 0, 0.3f, 0.3f);  // x in [0, 1000]
	int32 Changes = 0;
	const FDelegateHandle Handle = S.Zones->OnZoneSelectionChanged.AddLambda([&Changes, &S](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent*)
	{
		Changes += Player == S.Player ? 1 : 0;
	});
	for (const int32 Fps : { 30, 60, 120 })
	{
		const float Dt = 1.f / Fps;
		S.At(Far + FVector(-300.0, 0.0, 0.0), Dt, nullptr, /*bTeleport*/ true); // start in A at once
		TestTrue(FString::Printf(TEXT("%d fps: in A"), Fps), S.Selected() == A->GetZone());
		Changes = 0;
		const int32 Requests = S.Seen.Num();
		// +-100 cm around the boundary every 0.2 s: beyond A's margin (B is the candidate) for ~0.07 s at a time.
		const int32 Frames = 2 * Fps;
		for (int32 I = 0; I < Frames; ++I)
		{
			const double X = 100.0 * FMath::Sin(2.0 * UE_DOUBLE_PI * (I * Dt) / 0.2);
			S.At(Far + FVector(X, 0.0, 0.0), Dt);
		}
		TestEqual(FString::Printf(TEXT("%d fps: 2 s of jitter on the boundary, no selection change"), Fps), Changes, 0);
		TestEqual(FString::Printf(TEXT("%d fps: no new request"), Fps), S.Seen.Num(), Requests);
		TestTrue(FString::Printf(TEXT("%d fps: still A"), Fps), S.Selected() == A->GetZone());

		// Stay 100 cm into B: B after max(A's Exit Delay, B's Enter Delay) = 0.3 s.
		int32 Waited = 0;
		while (S.At(Far + FVector(100.0, 0.0, 0.0), Dt) != B->GetZone() && Waited < 4 * Fps)
		{
			++Waited;
		}
		const float Seconds = Waited * Dt;
		TestTrue(FString::Printf(TEXT("%d fps: B after %.4f s (0.3 s, at most one frame later)"), Fps, Seconds), Seconds >= 0.3f - 1e-3f && Seconds <= 0.3f + Dt + 1e-3f);
		TestEqual(FString::Printf(TEXT("%d fps: one request for B"), Fps), S.Seen.Num(), Requests + 1);
		S.At(Far, 0.f, nullptr, true);
		S.Zones->UpdatePlayer(S.Player, static_cast<const FVector*>(nullptr));
	}
	S.Zones->OnZoneSelectionChanged.Remove(Handle);
	S.Cleanup(*this);
	return true;
}

// The candidate changes while it waits: the wait restarts; the candidate going back to the selection drops it; zone A to B
// waits max(A's Exit Delay, B's Enter Delay); two players wait independently.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneDwellCandidateTest, "ActionCameraFramework.Zone.Dwell.CandidateChangesWhileWaiting", ACFZoneDwellTests::Flags)
bool FACFZoneDwellCandidateTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneDwellTests;
	FSync S;
	if (!S.Init(*this))
	{
		return true;
	}
	constexpr float Dt = 0.05f;
	// A: x in [0, 2000], priority 0, Enter 0.5 / Exit 0.2. B inside A: x in [800, 1200], priority 1, Enter 0.6 / Exit 0.4.
	AACFCameraZone* A = S.Box(Far + FVector(1000.0, 0.0, 0.0), FVector(1000.0, 500.0, 500.0), 0, 0.5f, 0.2f);
	AACFCameraZone* B = S.Box(Far + FVector(1000.0, 0.0, 0.0), FVector(200.0, 500.0, 500.0), 1, 0.6f, 0.4f);
	const FVector Out = Far + FVector(-500.0, 0.0, 0.0);
	const FVector InA = Far + FVector(300.0, 0.0, 0.0);
	const FVector InB = Far + FVector(1000.0, 0.0, 0.0);
	auto Run = [&S](const FVector& Where, float Seconds) { for (int32 I = 0; I < FMath::RoundToInt(Seconds / Dt); ++I) { S.At(Where, Dt); } };
	UACFCameraZoneComponent* Pending = nullptr;
	float Elapsed = 0.f;

	S.At(Out, Dt, nullptr, true);
	S.At(InA, Dt);
	TestTrue(TEXT("Entering A: pending A"), S.Pending(Pending, Elapsed) && Pending == A->GetZone() && S.Selected() == nullptr);
	Run(InA, 0.3f);
	TestTrue(FString::Printf(TEXT("A waited 0.3 s (%.2f)"), Elapsed), S.Pending(Pending, Elapsed) && FMath::IsNearlyEqual(Elapsed, 0.3f, 1e-3f));
	// The candidate becomes B (inside A, higher priority): the wait restarts for B's 0.6 s; A is never selected.
	S.At(InB, Dt);
	TestTrue(TEXT("Candidate B: the wait restarts"), S.Pending(Pending, Elapsed) && Pending == B->GetZone() && Elapsed == 0.f);
	Run(InB, 0.5f);
	TestTrue(TEXT("0.5 s of B's 0.6 s: nothing selected"), S.Selected() == nullptr);
	Run(InB, 0.1f);
	TestTrue(TEXT("0.6 s: B"), S.Selected() == B->GetZone());
	TestEqual(TEXT("One request (B), none for A"), S.Seen.Num(), 1);

	// B -> A waits max(B's Exit 0.4, A's Enter 0.5) = 0.5 s. Back into B after 0.3 s: the change is dropped.
	Run(InA, 0.3f);
	TestTrue(TEXT("B -> A pending"), S.Pending(Pending, Elapsed) && Pending == A->GetZone() && S.Selected() == B->GetZone());
	S.At(InB, Dt);
	TestFalse(TEXT("Back in B: no pending change"), S.Pending(Pending, Elapsed));
	Run(InA, 0.5f); // the first update starts the wait (0 s), nine more: 0.45 s
	TestTrue(TEXT("0.45 s of 0.5 s: still B"), S.Selected() == B->GetZone());
	S.At(InA, Dt);
	TestTrue(TEXT("0.5 s: A"), S.Selected() == A->GetZone());

	// Leaving to no zone waits A's Exit Delay (0.2 s).
	Run(Out, 0.2f); // 0 .. 0.15 s
	TestTrue(TEXT("Leaving A: pending no zone"), S.Pending(Pending, Elapsed) && Pending == nullptr && S.Selected() == A->GetZone());
	S.At(Out, Dt);
	TestTrue(TEXT("0.2 s: no zone"), S.Selected() == nullptr);
	TestEqual(TEXT("Requests: B, A"), S.Seen.Num(), 2);
	TestFalse(TEXT("No zone request now"), S.Zones->GetZoneRequest(S.Player).IsValid());

	// A second player waits on its own.
	TStrongObjectPtr<ULocalPlayer> Second(NewObject<ULocalPlayer>(GEngine));
	S.At(Out, Dt, nullptr, true, Second.Get());
	S.At(InA, Dt);                                    // player 1 starts waiting for A
	Run(InA, 0.25f);
	S.At(InB, Dt, nullptr, false, Second.Get());       // player 2 starts waiting for B
	TestTrue(TEXT("Player 1 waits for A"), S.Pending(Pending, Elapsed) && Pending == A->GetZone());
	TestTrue(TEXT("Player 2 waits for B, from 0"), S.Pending(Pending, Elapsed, Second.Get()) && Pending == B->GetZone() && Elapsed == 0.f);
	Run(InA, 0.25f);
	TestTrue(TEXT("Player 1: A after its 0.5 s"), S.Selected() == A->GetZone());
	TestTrue(TEXT("Player 2: nothing (no time passed for it)"), S.Selected(Second.Get()) == nullptr);
	S.Zones->UpdatePlayer(Second.Get(), static_cast<const FVector*>(nullptr));
	S.Cleanup(*this);
	return true;
}

// A pending zone that is destroyed is dropped; the selected zone disabled, destroyed or streamed out is left at once (no
// delay), with its request.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneDwellRemovalTest, "ActionCameraFramework.Zone.Dwell.RemovalWhileWaiting", ACFZoneDwellTests::Flags)
bool FACFZoneDwellRemovalTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneDwellTests;
	FSync S;
	if (!S.Init(*this))
	{
		return true;
	}
	constexpr float Dt = 0.05f;
	AACFCameraZone* Outer = S.Box(Far, FVector(2000.0, 2000.0, 500.0), 0, 1.f, 1.f);
	AACFCameraZone* Inner = S.Box(Far, FVector(300.0, 300.0, 500.0), 1, 1.f, 1.f);
	UACFCameraZoneComponent* Pending = nullptr;
	float Elapsed = 0.f;
	S.At(Far + FVector(1500.0, 0.0, 0.0), Dt, nullptr, true);
	TestTrue(TEXT("Outer (teleport: at once)"), S.Selected() == Outer->GetZone());
	S.At(Far, Dt);
	TestTrue(TEXT("Inner pending"), S.Pending(Pending, Elapsed) && Pending == Inner->GetZone());
	Inner->Destroy();
	TestFalse(TEXT("Pending zone destroyed: no pending change, no reference"), S.Pending(Pending, Elapsed));
	TestTrue(TEXT("Outer kept"), S.Selected() == Outer->GetZone());

	const FACFCameraRequestHandle OuterRequest = S.Zones->GetZoneRequest(S.Player);
	Outer->GetZone()->SetZoneEnabled(false);
	TestTrue(TEXT("Selected zone disabled: left at once despite its 1 s Exit Delay"), S.Selected() == nullptr);
	TestFalse(TEXT("... and its request released"), S.Acf->GetDirector()->HasRequest(OuterRequest));
	Outer->GetZone()->SetZoneEnabled(true);
	TestTrue(TEXT("Enabled again: a normal candidate (Enter Delay 1 s)"), S.Selected() == nullptr && S.Pending(Pending, Elapsed) && Pending == Outer->GetZone());
	for (int32 I = 0; I < 20; ++I)
	{
		S.At(Far, Dt);
	}
	TestTrue(TEXT("After 1 s: Outer"), S.Selected() == Outer->GetZone());
	const FACFCameraRequestHandle Again = S.Zones->GetZoneRequest(S.Player);
	Outer->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestTrue(TEXT("Streamed out: left at once"), S.Selected() == nullptr);
	TestFalse(TEXT("... and its request released"), S.Acf->GetDirector()->HasRequest(Again));

	AACFCameraZone* Third = S.Box(Far, FVector(500.0, 500.0, 500.0), 0, 1.f, 1.f);
	S.At(Far + FVector(0.0, 0.0, 10.0), Dt, nullptr, true);
	const FACFCameraRequestHandle ThirdRequest = S.Zones->GetZoneRequest(S.Player);
	TestTrue(TEXT("Third selected"), S.Selected() == Third->GetZone() && ThirdRequest.IsValid());
	Third->Destroy();
	TestTrue(TEXT("Destroyed: left at once"), S.Selected() == nullptr);
	TestFalse(TEXT("... and its request released"), S.Acf->GetDirector()->HasRequest(ThirdRequest));
	S.Cleanup(*this);
	return true;
}

// Teleports (a jump of the same pawn, the hint) ignore the delays and Exit Margin; a new pawn starts without the old pawn's
// pending change; no pawn leaves at once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneDwellTeleportTest, "ActionCameraFramework.Zone.Dwell.TeleportAndPawnChange", ACFZoneDwellTests::Flags)
bool FACFZoneDwellTeleportTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneDwellTests;
	FSync S;
	if (!S.Init(*this))
	{
		return true;
	}
	constexpr float Dt = 1.f / 60.f;
	AACFCameraZone* A = S.Box(Far, FVector(2000.0, 2000.0, 500.0), 0, 2.f, 2.f, 100.0);
	AACFCameraZone* B = S.Box(Far + FVector(10000.0, 0.0, 0.0), FVector(500.0, 500.0, 500.0), 0, 2.f, 2.f);
	const AActor* Pawn1 = S.Dummy(Far);
	const AActor* Pawn2 = S.Dummy(Far);
	UACFCameraZoneComponent* Pending = nullptr;
	float Elapsed = 0.f;
	const int32 Teleports = S.Zones->GetNumTeleports(S.Player);

	TestTrue(TEXT("First pawn: selected at once (no Enter Delay)"), S.At(Far, Dt, Pawn1) == A->GetZone());
	S.At(Far + FVector(500.0, 0.0, 0.0), Dt, Pawn1);
	TestTrue(TEXT("A 500 cm move of the same pawn is no teleport"), S.Zones->GetNumTeleports(S.Player) == Teleports + 1 && S.Selected() == A->GetZone());
	// Jump to B: selected at once, without A's Exit and B's Enter Delay.
	TestTrue(TEXT("Teleport A -> B: B at once"), S.At(Far + FVector(10000.0, 0.0, 0.0), Dt, Pawn1) == B->GetZone());
	TestEqual(TEXT("Teleport counted"), S.Zones->GetNumTeleports(S.Player), Teleports + 2);
	// Jump back to 50 cm outside A (within its 100 cm Exit Margin, were it selected): no zone (margin ignored).
	TestTrue(TEXT("Teleport out of the zones: no zone at once"), S.At(Far + FVector(2050.0, 0.0, 0.0), Dt, Pawn1) == nullptr);
	TestTrue(TEXT("Teleport into A"), S.At(Far, Dt, Pawn1) == A->GetZone());
	// Jump to 50 cm outside A from inside: Exit Margin would keep A for a walk; a teleport leaves.
	TestTrue(TEXT("Teleport to within A's margin: left (margin ignored)"), S.At(Far + FVector(2050.0, 0.0, 0.0), Dt, Pawn1) == nullptr);
	// The hint (NotifyTeleport) without a big move.
	S.At(Far + FVector(2300.0, 0.0, 0.0), Dt, Pawn1);
	TestTrue(TEXT("Hint: a 250 cm move into A selects at once"), S.At(Far + FVector(1900.0, 0.0, 0.0), Dt, Pawn1, /*bTeleport*/ true) == A->GetZone());

	// Pawn 1 walks out of A (pending no zone, 2 s Exit Delay); the player takes pawn 2 inside A: the old pending change is gone.
	S.At(Far + FVector(2300.0, 0.0, 0.0), Dt, Pawn1);
	S.At(Far + FVector(2300.0, 0.0, 0.0), Dt, Pawn1);
	TestTrue(TEXT("Pawn 1 leaving A: pending"), S.Pending(Pending, Elapsed) && Pending == nullptr && S.Selected() == A->GetZone());
	S.At(Far, Dt, Pawn2);
	TestFalse(TEXT("New pawn: no pending change of the old pawn"), S.Pending(Pending, Elapsed));
	TestTrue(TEXT("New pawn in A"), S.Selected() == A->GetZone());
	// Pawn 2 at B's place: a new pawn far away is resolved at once too.
	TestTrue(TEXT("Back to pawn 1 (at B): B at once"), S.At(Far + FVector(10000.0, 0.0, 0.0), Dt, Pawn1) == B->GetZone());
	// No pawn: left at once (no Exit Delay); the request goes.
	const FACFCameraRequestHandle Request = S.Zones->GetZoneRequest(S.Player);
	TestTrue(TEXT("No pawn: no zone at once"), S.Zones->UpdatePlayer(S.Player, static_cast<const FVector*>(nullptr)) == nullptr);
	TestFalse(TEXT("No pawn: request released"), S.Acf->GetDirector()->HasRequest(Request));
	TestTrue(TEXT("The same pawn again (re-possessed): at once"), S.At(Far + FVector(10000.0, 0.0, 0.0), Dt, Pawn1) == B->GetZone());
	S.Cleanup(*this);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The frame driver with the real pawn: teleports cut the zone camera; a Lock-On / Finisher on screen stays in control;
// NotifyTeleport; pause keeps the wait; possession / pawn destruction.

namespace ACFZoneDwellTests
{
	struct FFlow
	{
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UACFCameraZoneSubsystem> Zones;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<UACFCameraSubsystem> Acf;
		TWeakObjectPtr<APawn> OriginalPawn;
		ACFTestEnvironment::FGlobalStateGuard Guard;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TArray<TStrongObjectPtr<UObject>> Keep;
		FVector Origin = FVector::ZeroVector;
		FRotator SavedControlRotation = FRotator::ZeroRotator;
		bool bStarted = false;
		uint64 Mark = 0;
		int32 Count = 0;
		int32 TeleportsMark = 0;
		float ElapsedMark = 0.f;
		double TimeMark = 0.0;
		bool bPaused = false;
		TWeakObjectPtr<AACFCameraZone> A;
		TWeakObjectPtr<AACFCameraZone> B;
		TWeakObjectPtr<APawn> NewPawn;
		FACFCameraRequestHandle Explore;
		FACFCameraRequestHandle LockOn;
		FACFCameraSequenceHandle Finisher;

		ULocalPlayer* Player() const { return Controller.IsValid() ? Controller->GetLocalPlayer() : nullptr; }
		UACFCameraDirector* Director() const { return Acf.IsValid() ? Acf->GetDirector() : nullptr; }
		APawn* Pawn() const { return Controller.IsValid() ? Controller->GetPawn() : nullptr; }
		APlayerCameraManager* PCM() const { return Controller.IsValid() ? Controller->PlayerCameraManager.Get() : nullptr; }
		bool Waited(uint64 Frames) const { return GFrameCounter >= Mark + Frames; }
		FACFCameraRequestHandle ZoneRequest() const { return Zones->GetZoneRequest(Player()); }

		bool Start(FAutomationTestBase& InTest)
		{
			Test = &InTest;
			World = ACFTestEnvironment::FindGameWorld();
			Zones = World.IsValid() ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
			Controller = World.IsValid() ? World->GetFirstPlayerController() : nullptr;
			Acf = Player() ? Player()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
			if (!World.IsValid() || !World->HasBegunPlay() || !Zones.IsValid() || !Acf.IsValid() || !Pawn() || !PCM())
			{
				InTest.AddWarning(TEXT("Skipped: needs a game World with the first player's pawn and camera (run with -game)."));
				return false;
			}
			Guard.Capture(World.Get());
			SavedControlRotation = Controller->GetControlRotation();
			ACFTestEnvironment::BeginScenario(*Acf);
			OriginalPawn = Pawn();
			Origin = Pawn()->GetActorLocation();
			bStarted = true;
			return true;
		}

		UACFCameraPreset* Fixed(const FVector& Location)
		{
			UACFCameraPreset* Preset = ACFTests::MakeFixedPreset(Location, 70.f);
			Keep.Emplace(Preset);
			return Preset;
		}

		AACFCameraZone* Zone(const FVector& Center, float EnterDelay, float BlendIn, const FVector& CameraLocation)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AACFCameraZone* Zone = World->SpawnActor<AACFCameraZone>(Center, FRotator::ZeroRotator, Params);
			Zone->GetBox()->SetBoxExtent(FVector(400.0, 400.0, 400.0), false);
			UACFCameraZoneComponent* Component = Zone->GetZone();
			Component->Preset = Fixed(CameraLocation);
			Component->EnterDelay = EnterDelay;
			Component->bOverrideBlendIn = BlendIn > 0.f;
			Component->BlendIn = ACFTests::Linear(BlendIn);
			Component->bOverrideBlendOut = true;
			Component->BlendOut = ACFTests::Linear(0.5f);
			Spawned.Add(Zone);
			return Zone;
		}

		void MovePawn(const FVector& Location)
		{
			if (APawn* P = Pawn())
			{
				P->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}

		void Cleanup()
		{
			if (!bStarted)
			{
				return;
			}
			bStarted = false;
			if (bPaused)
			{
				UGameplayStatics::SetGamePaused(World.Get(), false);
			}
			if (Controller.IsValid() && OriginalPawn.IsValid() && Controller->GetPawn() != OriginalPawn.Get())
			{
				Controller->Possess(OriginalPawn.Get());
			}
			if (OriginalPawn.IsValid())
			{
				OriginalPawn->SetActorLocation(Origin, false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (NewPawn.IsValid() && !NewPawn->IsActorBeingDestroyed())
			{
				NewPawn->Destroy();
			}
			for (const TWeakObjectPtr<AActor>& Actor : Spawned)
			{
				if (Actor.IsValid() && !Actor->IsActorBeingDestroyed())
				{
					Actor->Destroy();
				}
			}
			Spawned.Reset();
			if (Acf.IsValid())
			{
				if (Finisher.IsValid())
				{
					Acf->CancelCameraSequence(Finisher);
				}
				Acf->ReleaseCamera(LockOn);
				Acf->ReleaseCamera(Explore);
			}
			if (Controller.IsValid())
			{
				Controller->SetControlRotation(SavedControlRotation);
			}
			if (Test)
			{
				Test->TestFalse(TEXT("End: no zone request left"), Zones.IsValid() && ZoneRequest().IsValid());
				ACFTestEnvironment::EndScenario(*Test, Acf.Get(), PCM(), Guard);
			}
		}
	};

	using FStep = TFunction<bool(FFlow&)>;

	class FFlowCommand : public IAutomationLatentCommand
	{
	public:
		FFlowCommand(TSharedRef<FFlow> InFlow, TArray<FStep> InSteps) : Flow(InFlow), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			if (++Frames > 1500)
			{
				Flow->Test->AddError(FString::Printf(TEXT("Zone dwell flow: step %d timed out."), Index));
				Flow->Cleanup();
				return true;
			}
			if (!Flow->World.IsValid() || !Flow->Acf.IsValid() || !Flow->Zones.IsValid())
			{
				Flow->Test->AddError(TEXT("World, camera subsystem or zone subsystem lost during the flow."));
				Flow->Cleanup();
				return true;
			}
			if (Index < Steps.Num() && Steps[Index](*Flow))
			{
				Flow->Mark = GFrameCounter;
				++Index;
			}
			if (Index >= Steps.Num())
			{
				Flow->Cleanup();
				return true;
			}
			return false;
		}
	private:
		TSharedRef<FFlow> Flow;
		TArray<FStep> Steps;
		int32 Index = 0;
		int32 Frames = 0;
	};
}

// Teleports with the real pawn: zone to zone and out of the zones are cuts (no blend across the level); under a Lock-On the
// screen stays the Lock-On's and the zone keeps its Blend In for later; during a Finisher the finisher stays; NotifyTeleport.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneDwellWorldTeleportTest, "ActionCameraFramework.Zone.World.TeleportCuts", ACFZoneDwellTests::Flags)
bool FACFZoneDwellWorldTeleportTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneDwellTests;
	TSharedRef<FFlow> Flow = MakeShared<FFlow>();
	if (!Flow->Start(*this))
	{
		return true;
	}
	TArray<FStep> Steps;
	Steps.Add([](FFlow& F)
	{
		// The game's exploration camera (priority 0) and two zones 6000 cm apart with long Enter Delays and 0.5 s blends.
		FACFCameraRequestParams Params;
		Params.DebugName = TEXT("Explore");
		F.Explore = F.Acf->RequestCamera(F.Fixed(F.Origin + FVector(-300.0, 0.0, 300.0)), Params);
		F.A = F.Zone(F.Origin + FVector(0.0, 6000.0, 0.0), 3.f, 0.5f, F.Origin + FVector(0.0, 5400.0, 300.0));
		F.B = F.Zone(F.Origin + FVector(6000.0, 6000.0, 0.0), 3.f, 0.5f, F.Origin + FVector(6000.0, 5400.0, 300.0));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		F.TeleportsMark = F.Zones->GetNumTeleports(F.Player());
		F.MovePawn(F.Origin + FVector(0.0, 6000.0, 0.0)); // into A
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		const FACFCameraEvaluationResult Result = F.Acf->GetLastEvaluationResult();
		F.Test->TestTrue(TEXT("Teleport into A: selected at once (Enter Delay 3 s ignored)"), F.Zones->GetSelectedZone(F.Player()) == F.A->GetZone());
		F.Test->TestTrue(TEXT("Teleport detected by distance"), F.Zones->GetNumTeleports(F.Player()) > F.TeleportsMark);
		F.Test->TestTrue(TEXT("A in control"), Result.ActiveRequest == F.ZoneRequest());
		F.Test->TestFalse(TEXT("Cut: no blend (A's 0.5 s Blend In replaced)"), Result.bIsBlending);
		F.Test->TestTrue(TEXT("The real camera at A's camera"), F.PCM()->GetCameraLocation().Equals(F.Origin + FVector(0.0, 5400.0, 300.0), 1.0));
		F.MovePawn(F.Origin + FVector(6000.0, 6000.0, 0.0)); // A -> B
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		const FACFCameraEvaluationResult Result = F.Acf->GetLastEvaluationResult();
		F.Test->TestTrue(TEXT("Teleport A -> B: B at once"), F.Zones->GetSelectedZone(F.Player()) == F.B->GetZone() && Result.ActiveRequest == F.ZoneRequest());
		F.Test->TestFalse(TEXT("A -> B is a cut"), Result.bIsBlending);
		F.Test->TestTrue(TEXT("The real camera at B's camera"), F.PCM()->GetCameraLocation().Equals(F.Origin + FVector(6000.0, 5400.0, 300.0), 1.0));
		F.MovePawn(F.Origin); // out of the zones
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		const FACFCameraEvaluationResult Result = F.Acf->GetLastEvaluationResult();
		F.Test->TestTrue(TEXT("Teleport out: no zone, exploration camera"), F.Zones->GetSelectedZone(F.Player()) == nullptr && Result.ActiveRequest == F.Explore);
		F.Test->TestFalse(TEXT("Leaving by teleport is a cut (B's 0.5 s Blend Out replaced)"), Result.bIsBlending);
		// A Lock-On on screen, then a teleport into A: the Lock-On keeps the screen.
		FACFCameraRequestParams Params;
		Params.Priority = 10;
		Params.DebugName = TEXT("LockOn");
		F.LockOn = F.Acf->RequestCamera(F.Fixed(F.Origin + FVector(0.0, -500.0, 500.0)), Params);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.MovePawn(F.Origin + FVector(0.0, 6000.0, 0.0));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		const FACFCameraEvaluationResult Result = F.Acf->GetLastEvaluationResult();
		F.Test->TestTrue(TEXT("Under the Lock-On: A selected and requested"), F.Zones->GetSelectedZone(F.Player()) == F.A->GetZone() && F.ZoneRequest().IsValid());
		F.Test->TestTrue(TEXT("The Lock-On keeps the screen"), Result.ActiveRequest == F.LockOn && !Result.bIsBlending);
		F.Test->TestTrue(TEXT("The real camera at the Lock-On camera"), F.PCM()->GetCameraLocation().Equals(F.Origin + FVector(0.0, -500.0, 500.0), 1.0));
		// The game ends the Lock-On: A takes over with its own Blend In (the teleport cut was not kept for later).
		F.Acf->ReleaseCamera(F.LockOn);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(1); });
	Steps.Add([](FFlow& F)
	{
		const FACFCameraEvaluationResult Result = F.Acf->GetLastEvaluationResult();
		F.Test->TestTrue(TEXT("After the Lock-On: A"), Result.ActiveRequest == F.ZoneRequest());
		F.Test->TestTrue(TEXT("... with A's Blend In (not a cut)"), Result.bIsBlending);
		// A finisher, then a teleport into B: the finisher keeps the screen and returns to B.
		F.Finisher = F.Acf->StartFinisherCameraWithPreset(F.Fixed(F.Origin + FVector(500.0, 500.0, 500.0)), nullptr, nullptr, 0.6f, FACFCameraBlendSettings::MakeCut(), FACFCameraBlendSettings::MakeCut());
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.MovePawn(F.Origin + FVector(6000.0, 6000.0, 0.0));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("During the finisher: B selected"), F.Zones->GetSelectedZone(F.Player()) == F.B->GetZone());
		F.Test->TestTrue(TEXT("The finisher keeps the screen"), F.Acf->IsCameraSequencePlaying(F.Finisher)
			&& F.PCM()->GetCameraLocation().Equals(F.Origin + FVector(500.0, 500.0, 500.0), 1.0));
		return true;
	});
	Steps.Add([](FFlow& F) { return !F.Acf->IsCameraSequencePlaying(F.Finisher) && F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.Finisher = FACFCameraSequenceHandle();
		F.Test->TestTrue(TEXT("After the finisher: B"), F.Acf->GetLastEvaluationResult().ActiveRequest == F.ZoneRequest());
		// NotifyTeleport: a 300 cm move out of B's side... into A's area is not needed: a short move into A with the game's
		// teleport notification selects A at once (Enter Delay 3 s).
		F.MovePawn(F.Origin + FVector(0.0, 7000.0, 0.0)); // 1000 cm from A's center: outside A (extent 400), a normal walk out of B
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.MovePawn(F.Origin + FVector(0.0, 6300.0, 0.0)); // 700 cm: below Teleport Distance, into A
		F.Acf->NotifyTeleport(true);
		F.TeleportsMark = F.Zones->GetNumTeleports(F.Player());
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("NotifyTeleport: A at once (no 3 s Enter Delay)"), F.Zones->GetSelectedZone(F.Player()) == F.A->GetZone());
		F.Test->TestTrue(TEXT("NotifyTeleport counted"), F.Zones->GetNumTeleports(F.Player()) > F.TeleportsMark);
		F.MovePawn(F.Origin);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	return true;
}

// Pause keeps the wait (no time passes); a new possessed pawn keeps the zone request with the new follow target; the pawn
// destroyed releases it at once; re-possessing selects at once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneDwellWorldPauseTest, "ActionCameraFramework.Zone.World.PauseAndPossession", ACFZoneDwellTests::Flags)
bool FACFZoneDwellWorldPauseTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneDwellTests;
	TSharedRef<FFlow> Flow = MakeShared<FFlow>();
	if (!Flow->Start(*this))
	{
		return true;
	}
	TArray<FStep> Steps;
	Steps.Add([](FFlow& F)
	{
		F.A = F.Zone(F.Origin, 0.5f, 0.f, F.Origin + FVector(-600.0, 0.0, 300.0)); // around the pawn: pending A
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		UACFCameraZoneComponent* Pending = nullptr;
		F.Test->TestTrue(TEXT("Waiting for A"), F.Zones->GetPendingZone(F.Player(), Pending, F.ElapsedMark) && Pending == F.A->GetZone() && F.Zones->GetSelectedZone(F.Player()) == nullptr);
		F.bPaused = UGameplayStatics::SetGamePaused(F.World.Get(), true);
		if (!F.bPaused)
		{
			F.Test->AddWarning(TEXT("The game mode refused to pause: pause check skipped."));
		}
		UACFCameraZoneComponent* Unused = nullptr;
		F.Zones->GetPendingZone(F.Player(), Unused, F.ElapsedMark);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(45); });
	Steps.Add([](FFlow& F)
	{
		UACFCameraZoneComponent* Pending = nullptr;
		float Elapsed = 0.f;
		if (F.bPaused)
		{
			F.Test->TestTrue(FString::Printf(TEXT("Paused 45 frames: the wait kept (%.3f == %.3f), nothing selected"), Elapsed, F.ElapsedMark),
				F.Zones->GetPendingZone(F.Player(), Pending, Elapsed) && Pending == F.A->GetZone() && FMath::IsNearlyEqual(Elapsed, F.ElapsedMark, 1e-4f) && F.Zones->GetSelectedZone(F.Player()) == nullptr);
			UGameplayStatics::SetGamePaused(F.World.Get(), false);
			F.bPaused = false;
		}
		F.TimeMark = F.World->GetTimeSeconds();
		return true;
	});
	// Game time, not frames: -NullRHI runs at hundreds of frames per second.
	Steps.Add([](FFlow& F) { return F.Zones->GetSelectedZone(F.Player()) == F.A->GetZone() || F.World->GetTimeSeconds() > F.TimeMark + 3.0; });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Resumed: A selected after its delay"), F.Zones->GetSelectedZone(F.Player()) == F.A->GetZone() && F.ZoneRequest().IsValid());
		// Possess a new pawn inside A.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APawn* New = F.World->SpawnActor<APawn>(F.OriginalPawn->GetClass(), FTransform(F.Origin + FVector(0.0, 150.0, 0.0)), Params);
		if (!F.Test->TestNotNull(TEXT("New pawn"), New))
		{
			return true;
		}
		F.NewPawn = New;
		F.TeleportsMark = F.Zones->GetNumTeleports(F.Player());
		F.Count = F.ZoneRequest().GetId();
		F.Controller->Possess(New);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		if (!F.NewPawn.IsValid())
		{
			return true;
		}
		TArray<FACFCameraRequestInfo> Infos;
		F.Director()->GetRequestInfos(Infos);
		const FACFCameraRequestHandle Handle = F.ZoneRequest();
		const FACFCameraRequestInfo* Info = Infos.FindByPredicate([Handle](const FACFCameraRequestInfo& Entry) { return Entry.Handle == Handle; });
		F.Test->TestTrue(TEXT("New pawn: seen as a pawn change"), F.Zones->GetNumTeleports(F.Player()) > F.TeleportsMark);
		F.Test->TestTrue(TEXT("Same zone: the same request kept"), Handle.IsValid() && Handle.GetId() == F.Count);
		F.Test->TestTrue(FString::Printf(TEXT("Follow target = the new pawn (%s)"), Info ? *Info->FollowTarget : TEXT("-")), Info && Info->FollowTarget.Contains(F.NewPawn->GetActorNameOrLabel()));
		F.NewPawn->Destroy(); // the controller has no pawn now
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("No pawn: no zone, no zone request"), F.Zones->GetSelectedZone(F.Player()) == nullptr && !F.ZoneRequest().IsValid());
		F.TeleportsMark = F.Zones->GetNumTeleports(F.Player());
		F.Controller->Possess(F.OriginalPawn.Get());
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Re-possessed: A at once (no 0.5 s Enter Delay), requested"), F.Zones->GetSelectedZone(F.Player()) == F.A->GetZone() && F.ZoneRequest().IsValid());
		F.Test->TestTrue(TEXT("Re-possession counted as a pawn change"), F.Zones->GetNumTeleports(F.Player()) > F.TeleportsMark);
		F.A->Destroy();
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
