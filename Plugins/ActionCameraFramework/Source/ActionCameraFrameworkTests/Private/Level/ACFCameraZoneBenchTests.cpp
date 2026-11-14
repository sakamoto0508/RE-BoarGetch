#include "Misc/AutomationTest.h"
#include "ACFCameraProfiler.h"

#if WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER

#include "ACFCameraTestEnvironment.h"
#include "Core/ACFCameraTestUtils.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneSubsystem.h"
#include "Components/BoxComponent.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7C-3B: cost of the Camera Zone frame driver (UACFCameraZoneSubsystem::Tick) with 0 / 10 / 50 / 100 / 250 / 500 zones
 * and 1 / 2 local players (README 32 章 "性能"). NOT a regression test: it runs only with -ACFProfile (the profile context)
 * and writes Saved/ACFProfile/7C3B_Zones.md. It fails only if a steady frame allocates.
 *
 * The real players' pawns are moved into a grid of box zones far below the map; the subsystem's Tick is called directly (no
 * engine frame in between), so each sample is exactly one zone update of every player. Scenarios:
 *   Inside  - every pawn inside 2-3 overlapping zones, the selection and the zone request unchanged (the normal frame);
 *   Outside - every pawn outside all zones (every shape tested, no candidate);
 *   Walking - the first pawn walks 6 cm per frame across the grid (selection / request changes happen sometimes).
 * Timing: 1500 measured updates after 60 warm-up updates, FPlatformTime::Cycles64 around Tick, the zone cost counters
 * (geometry / resolver / request, UACFCameraZoneSubsystem::SetCostRecording) in a separate pass. Allocations: 300 updates with
 * the ACF allocation counter (FACFCameraAllocationCounter) under a profiler scope.
 */
namespace ACFZoneBench
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	const FVector Origin(0.0, 0.0, -400000.0);
	constexpr double Cell = 1000.0;
	constexpr int32 Columns = 25;
	constexpr float Dt = 1.f / 60.f;

	struct FRow
	{
		int32 Players = 0;
		int32 Zones = 0;
		FString Scenario;
		FACFCameraProfileStats TickUs;
		double GeometryUs = 0.0;
		double ResolverUs = 0.0;
		double RequestUs = 0.0;
		double ZonesTested = 0.0;
		double Candidates = 0.0;
		uint32 Allocations = 0;
		bool bCountedAllocations = false;
	};

	static FVector CellCenter(int32 Index)
	{
		return Origin + FVector((Index % Columns) * Cell, (Index / Columns) * Cell, 0.0);
	}

	/** N zones: one per cell, half extent 450..700 cm (neighbours overlap), priorities 0..2. */
	static void SpawnZones(UWorld* World, int32 Count, UACFCameraPreset* Preset, TArray<TWeakObjectPtr<AActor>>& Out)
	{
		FRandomStream Random(7300 + Count);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 I = 0; I < Count; ++I)
		{
			AACFCameraZone* Zone = World->SpawnActor<AACFCameraZone>(CellCenter(I), FRotator(0.0, Random.FRandRange(0.0, 45.0), 0.0), Params);
			Zone->GetBox()->SetBoxExtent(FVector(Random.FRandRange(450.0, 700.0), Random.FRandRange(450.0, 700.0), 400.0), false);
			Zone->GetZone()->ZonePriority = I % 3;
			Zone->GetZone()->Preset = Preset;
			Out.Add(Zone);
		}
	}

	static double CyclesToUs(uint64 Cycles) { return FPlatformTime::ToMilliseconds64(Cycles) * 1000.0; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneBenchTest, "ActionCameraFramework.Profile.Zones", ACFZoneBench::Flags)
bool FACFZoneBenchTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneBench;
	if (!FParse::Param(FCommandLine::Get(), TEXT("ACFProfile")))
	{
		AddInfo(TEXT("Skipped: Phase 7C-3B measurement, run with -ACFProfile."));
		return true;
	}
	UWorld* World = ACFTestEnvironment::FindGameWorld();
	UACFCameraZoneSubsystem* Zones = World ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
	APlayerController* PC1 = World ? World->GetFirstPlayerController() : nullptr;
	if (!Zones || !PC1 || !PC1->GetPawn() || Zones->GetNumZones() > 10)
	{
		AddWarning(TEXT("Skipped: needs a game World with the first player's pawn (and no more than the sample zones)."));
		return true;
	}
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this);
	const int32 BaseZones = Zones->GetNumZones(); // the sample zones of L_ACFCameraTest, far from the grid
	TStrongObjectPtr<UACFCameraPreset> Preset(ACFTests::MakeFixedPreset(Origin + FVector(0.0, 0.0, 1500.0), 70.f));
	const bool bCounter = FACFCameraAllocationCounter::Install();
	FACFCameraProfiler Profiler;
	uint64 ProfileFrame = 90000000ull;

	struct FSaved { TWeakObjectPtr<APawn> Pawn; FVector Location; FRotator Rotation; };
	TArray<FSaved> Saved;
	Saved.Add({ PC1->GetPawn(), PC1->GetPawn()->GetActorLocation(), PC1->GetPawn()->GetActorRotation() });
	ULocalPlayer* Second = nullptr;
	TArray<FRow> Rows;
	const int32 Counts[] = { 0, 10, 50, 100, 250, 500 };

	for (int32 NumPlayers = 1; NumPlayers <= 2; ++NumPlayers)
	{
		if (NumPlayers == 2)
		{
			FString Error;
			Second = World->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
			APawn* Pawn2 = Second && Second->PlayerController ? Second->PlayerController->GetPawn() : nullptr;
			if (!Pawn2)
			{
				AddWarning(FString::Printf(TEXT("No second local player with a pawn (%s): 2-player rows skipped."), *Error));
				break;
			}
		}
		TArray<APawn*> Pawns = { PC1->GetPawn() };
		if (NumPlayers == 2)
		{
			Pawns.Add(Second->PlayerController->GetPawn());
		}
		for (const int32 Count : Counts)
		{
			TArray<TWeakObjectPtr<AActor>> Spawned;
			SpawnZones(World, Count, Preset.Get(), Spawned);
			for (const TCHAR* Scenario : { TEXT("Inside"), TEXT("Outside"), TEXT("Walking") })
			{
				const bool bInside = FCString::Strcmp(Scenario, TEXT("Inside")) == 0;
				const bool bWalking = FCString::Strcmp(Scenario, TEXT("Walking")) == 0;
				auto Place = [&](int32 Step)
				{
					for (int32 P = 0; P < Pawns.Num(); ++P)
					{
						FVector Where = bInside && Count > 0 ? CellCenter(FMath::Min(P * 7 + 1, Count - 1)) + FVector(Cell * 0.5, 0.0, 0.0) // between two cells: overlaps
							: Origin + FVector(-5000.0, -5000.0 - P * 1000.0, 0.0);
						if (bWalking && P == 0)
						{
							Where = Origin + FVector(-600.0 + Step * 6.0, Cell * 0.5, 0.0); // 6 cm per frame along the first row
						}
						Pawns[P]->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
					}
				};
				int32 Step = 0;
				for (int32 I = 0; I < 60; ++I) // warm up: the first update after a move is a teleport, requests are made
				{
					Place(Step++);
					Zones->Tick(Dt);
				}
				TArray<double> Us;
				Us.Reserve(1500);
				for (int32 I = 0; I < 1500; ++I)
				{
					Place(Step++);
					const uint64 T0 = FPlatformTime::Cycles64();
					Zones->Tick(Dt);
					Us.Add(CyclesToUs(FPlatformTime::Cycles64() - T0));
				}
				FRow Row;
				Row.Players = NumPlayers;
				Row.Zones = Count;
				Row.Scenario = Scenario;
				Row.TickUs = FACFCameraProfileStats::Compute(Us);
				// Breakdown pass (the counters add their own Cycles64 calls: not used for the totals above).
				Zones->SetCostRecording(true);
				for (int32 I = 0; I < 500; ++I)
				{
					Place(Step++);
					Zones->Tick(Dt);
				}
				const FACFCameraZoneCost Cost = Zones->GetCost();
				Zones->SetCostRecording(false);
				const double Updates = FMath::Max(Cost.Updates, 1);
				Row.GeometryUs = CyclesToUs(Cost.GeometryCycles) / Updates;
				Row.ResolverUs = CyclesToUs(Cost.ResolverCycles) / Updates;
				Row.RequestUs = CyclesToUs(Cost.RequestCycles) / Updates;
				Row.ZonesTested = Cost.ZonesTested / Updates;
				Row.Candidates = Cost.Candidates / Updates;
				if (bCounter && !bWalking)
				{
					Profiler.Start(512);
					FACFCameraAllocationCounter::SetCounting(true);
					for (int32 I = 0; I < 300; ++I)
					{
						Place(Step++);
						FACFCameraProfiler::FActiveScope Scope(&Profiler, ProfileFrame++, Dt, /*bIsEvaluation*/ true, /*bBackground*/ false);
						Zones->Tick(Dt);
					}
					FACFCameraAllocationCounter::SetCounting(false);
					for (int32 I = 0; I < Profiler.Num(); ++I)
					{
						Row.Allocations += Profiler.GetFrame(I).Allocations;
					}
					Profiler.Stop();
					Row.bCountedAllocations = true;
					TestEqual(FString::Printf(TEXT("%d player(s), %d zones, %s: no allocation in 300 steady updates"), NumPlayers, Count, Scenario), Row.Allocations, 0u);
				}
				Rows.Add(Row);
			}
			for (const TWeakObjectPtr<AActor>& Actor : Spawned)
			{
				if (Actor.IsValid())
				{
					Actor->Destroy();
				}
			}
			TestEqual(FString::Printf(TEXT("%d zones removed"), Count), Zones->GetNumZones(), BaseZones);
		}
	}

	// Back: pawns where they were, the second player removed, the zone requests released by one more update.
	for (const FSaved& S : Saved)
	{
		if (S.Pawn.IsValid())
		{
			S.Pawn->SetActorLocation(S.Location, false, nullptr, ETeleportType::TeleportPhysics);
			S.Pawn->SetActorRotation(S.Rotation);
		}
	}
	if (Second)
	{
		Zones->RemovePlayer(Second);
		ACFTestEnvironment::RemoveLocalPlayer(Second);
	}
	Zones->Tick(Dt);

	FString Table;
	for (const FRow& R : Rows)
	{
		Table += FString::Printf(TEXT("| %d | %d | %s | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.0f | %.1f | %s |\n"), R.Players, R.Zones, *R.Scenario, R.TickUs.Median, R.TickUs.P95, R.TickUs.Max,
			R.GeometryUs, R.ResolverUs, R.RequestUs, R.ZonesTested, R.Candidates, R.bCountedAllocations ? *FString::FromInt(R.Allocations) : TEXT("-"));
	}
	const FString Text = FString::Printf(TEXT("# Phase 7C-3B: Camera Zone frame driver cost\n\n")
		TEXT("Environment: %s, map %s, %s. Development editor binary (not Shipping). UACFCameraZoneSubsystem::Tick called directly; 1500 measured updates per row after 60 warm-up updates; ")
		TEXT("breakdown from 500 more updates with the cost counters (per player update); allocations from 300 updates with the ACF allocation counter. The %d sample zones of the map are registered too (far away).\n\n")
		TEXT("| players | zones | scenario | Tick p50 us | p95 | max | geometry us / update | resolver us | request us | zones tested / update | candidates / update | allocations (300 updates) |\n")
		TEXT("|---|---|---|---|---|---|---|---|---|---|---|---|\n%s"),
		GIsEditor ? TEXT("editor") : TEXT("-game"), *World->GetMapName(), FApp::CanEverRender() ? TEXT("rendering") : TEXT("-NullRHI"), BaseZones, *Table);
	FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("ACFProfile") / TEXT("7C3B_Zones.md")));
	AddInfo(Text);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER
