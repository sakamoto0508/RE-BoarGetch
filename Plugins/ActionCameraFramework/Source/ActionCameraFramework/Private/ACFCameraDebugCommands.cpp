// Development-only console commands for exercising ACF without any project content.
//   ACF.Debug.PushFixed X Y Z Pitch Yaw Roll FOV [Priority] [BlendSeconds]
//   ACF.Debug.ReleaseLast [BlendSeconds]
//   ACF.Debug.ReleaseAll [Immediate(0/1)]
//   ACF.Debug.Dump
// They act on the first local player of the world the command runs in.

#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "UObject/StrongObjectPtr.h"
#include "ACFCameraSettings.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraProfiler.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if !UE_BUILD_SHIPPING

namespace ACFDebug
{
	static TArray<FACFCameraRequestHandle> GDebugHandles;

	static UACFCameraSubsystem* FindSubsystem(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		UACFCameraSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Debug: no local player in world %s."), *GetNameSafe(World));
		}
		return Subsystem;
	}

	static float ArgOr(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default;
	}

	static FACFCameraBlendSettings MakeLinear(float Seconds)
	{
		FACFCameraBlendSettings Blend;
		Blend.Curve = Seconds > 0.f ? EACFCameraBlendCurve::Linear : EACFCameraBlendCurve::Cut;
		Blend.Duration = FMath::Max(Seconds, 0.f);
		return Blend;
	}

	static void PushFixed(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 7)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Debug.PushFixed X Y Z Pitch Yaw Roll FOV [Priority] [BlendSeconds]"));
			return;
		}
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		if (!Subsystem)
		{
			return;
		}

		// Transient preset; kept alive by the director's request entry while the request exists.
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UACFCameraPreset::StaticClass(), TEXT("ACFDebugFixedPreset")), RF_Transient);
		UACFCameraNode_FixedPose* Node = NewObject<UACFCameraNode_FixedPose>(Preset);
		Node->Location = FVector(ArgOr(Args, 0, 0.f), ArgOr(Args, 1, 0.f), ArgOr(Args, 2, 0.f));
		Node->Rotation = FRotator(ArgOr(Args, 3, 0.f), ArgOr(Args, 4, 0.f), ArgOr(Args, 5, 0.f));
		Node->FieldOfView = ArgOr(Args, 6, 90.f);
		Preset->Nodes.Add(Node);
		const float BlendSeconds = ArgOr(Args, 8, 0.f);
		Preset->DefaultBlendIn = MakeLinear(BlendSeconds);
		Preset->DefaultBlendOut = MakeLinear(BlendSeconds);

		FACFCameraRequestParams Params;
		Params.Priority = static_cast<int32>(ArgOr(Args, 7, 0.f));
		Params.DebugName = TEXT("ACF.Debug.PushFixed");
		const FACFCameraRequestHandle Handle = Subsystem->RequestCamera(Preset, Params);
		GDebugHandles.Add(Handle);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.PushFixed -> %s loc=%s rot=%s fov=%.2f priority=%d blend=%.2f"),
			*Handle.ToString(), *Node->Location.ToString(), *Node->Rotation.ToString(), Node->FieldOfView, Params.Priority, BlendSeconds);
	}

	static void ReleaseLast(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		if (!Subsystem || GDebugHandles.IsEmpty())
		{
			return;
		}
		FACFCameraRequestHandle Handle = GDebugHandles.Pop();
		const FString Name = Handle.ToString();
		const bool bRemoved = Args.IsValidIndex(0)
			? Subsystem->ReleaseCameraWithBlend(Handle, MakeLinear(ArgOr(Args, 0, 0.f)))
			: Subsystem->ReleaseCamera(Handle);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.ReleaseLast %s removed=%d"), *Name, bRemoved ? 1 : 0);
	}

	static void ReleaseAll(const TArray<FString>& Args, UWorld* World)
	{
		if (UACFCameraSubsystem* Subsystem = FindSubsystem(World))
		{
			Subsystem->ReleaseAllCameras(ArgOr(Args, 0, 0.f) != 0.f);
			GDebugHandles.Reset();
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.ReleaseAll"));
		}
	}

	static void Dump(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		if (!Subsystem)
		{
			return;
		}
		const FACFCameraEvaluationResult Result = Subsystem->GetLastEvaluationResult();
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.Dump frame=%llu active=%s requests=%d weight=%.3f blending=%d alpha=%.3f pose loc=%s rot=%s fov=%.3f"),
			Result.FrameNumber, *Result.ActiveRequest.ToString(), Result.NumRequests, Result.Weight, Result.bIsBlending ? 1 : 0, Result.BlendAlpha,
			*Result.Pose.Location.ToString(), *Result.Pose.Rotation.ToString(), Result.Pose.FieldOfView);

		const APlayerController* PC = World->GetFirstPlayerController();
		if (const APlayerCameraManager* PCM = PC ? PC->PlayerCameraManager.Get() : nullptr)
		{
			// What UE will actually render (camera cache filled at the end of UpdateCamera).
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.Dump PCM=%s viewtarget=%s camera loc=%s rot=%s fov=%.3f"),
				*PCM->GetName(), *GetNameSafe(PCM->GetViewTarget()),
				*PCM->GetCameraLocation().ToString(), *PCM->GetCameraRotation().ToString(), PCM->GetFOVAngle());
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GPushFixedCmd(TEXT("ACF.Debug.PushFixed"),
		TEXT("Push a fixed ACF camera: X Y Z Pitch Yaw Roll FOV [Priority] [BlendSeconds]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PushFixed));
	static FAutoConsoleCommandWithWorldAndArgs GReleaseLastCmd(TEXT("ACF.Debug.ReleaseLast"),
		TEXT("Release the most recent ACF.Debug.PushFixed request: [BlendSeconds]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ReleaseLast));
	static FAutoConsoleCommandWithWorldAndArgs GReleaseAllCmd(TEXT("ACF.Debug.ReleaseAll"),
		TEXT("Release all ACF requests of the first local player: [Immediate 0/1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ReleaseAll));
	static FAutoConsoleCommandWithWorldAndArgs GDumpCmd(TEXT("ACF.Debug.Dump"),
		TEXT("Log the last ACF evaluation result and the PlayerCameraManager's final camera."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dump));
}

namespace ACFDebug
{
	// ---------------------------------------------------------------------------------------------
	// Phase 6B / B-1 profiler (README "計測"). Recording itself is switched by the cvar ACF.Profile 0/1.
	//   ACF.Profile.Report [Name]       summary per local player (+ all players per frame) to the log, CSV to Saved/ACFProfile/
	//   ACF.Profile.Clear               drops the recorded frames
	//   ACF.Profile.Allocations <0|1>   counts game-thread allocations inside ACF scopes (installs the counting allocator once)
	//   ACF.Profile.Sites [N]           call stacks of the allocations counted so far

	static void ForEachDirector(UWorld* World, TFunctionRef<void(int32 PlayerIndex, UACFCameraDirector& Director)> Visit)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			return;
		}
		const TArray<ULocalPlayer*>& Players = GameInstance->GetLocalPlayers();
		for (int32 Index = 0; Index < Players.Num(); ++Index)
		{
			const UACFCameraSubsystem* Subsystem = Players[Index] ? Players[Index]->GetSubsystem<UACFCameraSubsystem>() : nullptr;
			if (UACFCameraDirector* Director = Subsystem ? Subsystem->GetDirector() : nullptr)
			{
				Visit(Index, *Director);
			}
		}
	}

	static void ProfileReport(const TArray<FString>& Args, UWorld* World)
	{
		const FString Name = Args.IsValidIndex(0) ? Args[0] : TEXT("ACFProfile");
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("ACFProfile");
		TMap<uint64, double> TotalUsByFrame;
		TMap<uint64, int32> QueriesByFrame;
		ForEachDirector(World, [&](int32 PlayerIndex, UACFCameraDirector& Director)
		{
			const FACFCameraProfiler& Profiler = Director.GetProfiler();
			TArray<const FACFCameraProfileFrame*> Frames;
			Profiler.GetFrames(Frames);
			for (const FACFCameraProfileFrame* Frame : Frames)
			{
				TotalUsByFrame.FindOrAdd(Frame->FrameNumber) += Frame->GetTotalMicroseconds();
				QueriesByFrame.FindOrAdd(Frame->FrameNumber) += Frame->GetFrameQueries();
			}
			const FACFCameraProfileSummary Summary = FACFCameraProfileSummary::Summarize(Frames);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Report %s player %d:\n%s"), *Name, PlayerIndex, *Summary.ToString());
			const FString Path = Folder / FString::Printf(TEXT("%s_P%d.csv"), *Name, PlayerIndex);
			FFileHelper::SaveStringToFile(ACFCameraProfiling::BuildCsv(Profiler), *Path);
		});
		TArray<double> Totals;
		TArray<double> Queries;
		for (const TPair<uint64, double>& Pair : TotalUsByFrame)
		{
			Totals.Add(Pair.Value);
			Queries.Add(QueriesByFrame[Pair.Key]);
		}
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Report %s all players per frame: total us %s, queries %s (CSV in %s)"), *Name,
			*FACFCameraProfileStats::Compute(Totals).ToString(), *FACFCameraProfileStats::Compute(Queries).ToString(), *Folder);
	}

	static void ProfileClear(const TArray<FString>& Args, UWorld* World)
	{
		ForEachDirector(World, [](int32, UACFCameraDirector& Director) { Director.GetProfiler().Clear(); });
		FACFCameraAllocationCounter::ResetSites();
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Clear"));
	}

	static void ProfileAllocations(const TArray<FString>& Args, UWorld* World)
	{
		const bool bOn = !Args.IsValidIndex(0) || FCString::Atoi(*Args[0]) != 0;
		if (bOn && !FACFCameraAllocationCounter::Install())
		{
			return;
		}
		FACFCameraAllocationCounter::SetCounting(bOn);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Allocations %d (counts only while ACF.Profile records)"), bOn ? 1 : 0);
	}

	static void ProfileSites(const TArray<FString>& Args, UWorld* World)
	{
		const bool bWasCounting = FACFCameraAllocationCounter::IsCounting();
		FACFCameraAllocationCounter::SetCounting(false); // symbolizing allocates
		TArray<FACFCameraAllocationCounter::FSite> Sites;
		uint32 Overflow = 0;
		FACFCameraAllocationCounter::GetSites(Sites, Overflow);
		Sites.Sort([](const FACFCameraAllocationCounter::FSite& A, const FACFCameraAllocationCounter::FSite& B) { return A.Count > B.Count; });
		const int32 Max = Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : 10;
		for (int32 I = 0; I < FMath::Min(Max, Sites.Num()); ++I)
		{
			const FACFCameraAllocationCounter::FSite& Site = Sites[I];
			UE_LOG(LogActionCamera, Display, TEXT("ACF allocation site %d: %u allocations, %llu bytes, section %s\n%s"), I, Site.Count, Site.Bytes,
				Site.Section >= 0 ? LexToString(static_cast<EACFCameraProfileSection>(Site.Section)) : TEXT("(none)"), *FACFCameraAllocationCounter::DescribeSite(Site));
		}
		UE_LOG(LogActionCamera, Display, TEXT("ACF allocation sites: %d distinct, %u allocations at sites beyond the table"), Sites.Num(), Overflow);
		FACFCameraAllocationCounter::SetCounting(bWasCounting);
	}

	static FAutoConsoleCommandWithWorldAndArgs GProfileReportCmd(TEXT("ACF.Profile.Report"),
		TEXT("Summarizes the frames recorded with ACF.Profile 1 (log) and writes one CSV per local player to Saved/ACFProfile/. [Name]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileReport));
	static FAutoConsoleCommandWithWorldAndArgs GProfileClearCmd(TEXT("ACF.Profile.Clear"),
		TEXT("Drops the recorded profiler frames and allocation sites."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileClear));
	static FAutoConsoleCommandWithWorldAndArgs GProfileAllocationsCmd(TEXT("ACF.Profile.Allocations"),
		TEXT("<0|1> Counts game-thread heap allocations inside ACF scopes while ACF.Profile records."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileAllocations));
	static FAutoConsoleCommandWithWorldAndArgs GProfileSitesCmd(TEXT("ACF.Profile.Sites"),
		TEXT("[N] Logs the call stacks of the allocations counted by ACF.Profile.Allocations."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileSites));

	// Phase 6B / B-4: health counters per local player (the warnings themselves are logged at the 1st, 10th, 100th ... occurrence).
	static void HealthReport(const TArray<FString>& Args, UWorld* World)
	{
		ForEachDirector(World, [](int32 PlayerIndex, UACFCameraDirector& Director)
		{
			const FACFCameraHealthDiagnostics D = Director.GetHealthDiagnostics();
			UE_LOG(LogActionCamera, Display, TEXT("ACF health P%d: %d issues (targets %d, delta times %d, upstream %d, rejected inputs %d, node outputs %d, effects %d, final poses %d), fallback pose %s. Last: %s"),
				PlayerIndex, D.TotalIssues, D.InvalidTargets, D.InvalidDeltaTimes, D.InvalidUpstreamPoses, D.RejectedInputs, D.InvalidNodeOutputs, D.InvalidEffectOutputs,
				D.InvalidFinalPoses, Director.HasLastGoodPose() ? TEXT("available") : TEXT("none"), D.LastIssue.IsEmpty() ? TEXT("-") : *D.LastIssue);
		});
	}
	static FAutoConsoleCommandWithWorldAndArgs GHealthCmd(TEXT("ACF.Debug.Health"),
		TEXT("Logs the camera health counters (invalid values detected and kept out of the camera) of every local player."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HealthReport));
}

#endif // !UE_BUILD_SHIPPING
