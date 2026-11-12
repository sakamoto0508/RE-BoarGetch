#include "ACFCameraProfiler.h"
#include "ACFCameraTypes.h"
#include "HAL/PlatformTime.h"

// -------------------------------------------------------------------------------------------------
// Names and frame helpers (all configurations: reports can be built from saved frames anywhere)

const TCHAR* LexToString(EACFCameraProfileSection Section)
{
	switch (Section)
	{
	case EACFCameraProfileSection::Evaluate: return TEXT("Evaluate");
	case EACFCameraProfileSection::SequenceTick: return TEXT("SequenceTick");
	case EACFCameraProfileSection::ResolveRequests: return TEXT("ResolveRequests");
	case EACFCameraProfileSection::RigEvaluate: return TEXT("RigEvaluate");
	case EACFCameraProfileSection::Effects: return TEXT("Effects");
	case EACFCameraProfileSection::PostBlend: return TEXT("PostBlend");
	case EACFCameraProfileSection::Deocclude: return TEXT("Deocclude");
	case EACFCameraProfileSection::FramingRecovery: return TEXT("FramingRecovery");
	case EACFCameraProfileSection::ShotProbe: return TEXT("ShotProbe");
	case EACFCameraProfileSection::PreEvaluate: return TEXT("PreEvaluate");
	case EACFCameraProfileSection::BackgroundUpstream: return TEXT("BackgroundUpstream");
	case EACFCameraProfileSection::OutputGuard: return TEXT("OutputGuard");
	default: return TEXT("Unknown");
	}
}

const TCHAR* LexToString(EACFCameraQueryPurpose Purpose)
{
	switch (Purpose)
	{
	case EACFCameraQueryPurpose::Collision: return TEXT("Collision");
	case EACFCameraQueryPurpose::DeoccluderCheck: return TEXT("DeoccluderCheck");
	case EACFCameraQueryPurpose::DeoccluderSearch: return TEXT("DeoccluderSearch");
	case EACFCameraQueryPurpose::OutputGuard: return TEXT("OutputGuard");
	case EACFCameraQueryPurpose::ShotProbe: return TEXT("ShotProbe");
	default: return TEXT("Unknown");
	}
}

const TCHAR* LexToString(EACFCameraQueryShape Shape)
{
	return Shape == EACFCameraQueryShape::Sweep ? TEXT("Sweep") : TEXT("Line");
}

double FACFCameraProfileFrame::GetSectionMicroseconds(EACFCameraProfileSection Section) const
{
	return FPlatformTime::ToMilliseconds64(SectionCycles[static_cast<int32>(Section)]) * 1000.0;
}

int32 FACFCameraProfileFrame::GetQueries(EACFCameraQueryPurpose Purpose) const
{
	int32 Sum = 0;
	for (int32 Shape = 0; Shape < NumShapes; ++Shape)
	{
		Sum += Queries[static_cast<int32>(Purpose)][Shape];
	}
	return Sum;
}

int32 FACFCameraProfileFrame::GetQueries(EACFCameraQueryShape Shape) const
{
	int32 Sum = 0;
	for (int32 Purpose = 0; Purpose < NumPurposes; ++Purpose)
	{
		Sum += Queries[Purpose][static_cast<int32>(Shape)];
	}
	return Sum;
}

int32 FACFCameraProfileFrame::GetFrameQueries() const
{
	int32 Sum = 0;
	for (int32 Purpose = 0; Purpose < NumPurposes; ++Purpose)
	{
		Sum += GetQueries(static_cast<EACFCameraQueryPurpose>(Purpose));
	}
	return Sum;
}

int32 FACFCameraProfileFrame::GetEvaluationQueries() const
{
	return GetFrameQueries() - GetQueries(EACFCameraQueryPurpose::OutputGuard);
}

double FACFCameraProfileFrame::GetTotalMicroseconds() const
{
	return GetSectionMicroseconds(EACFCameraProfileSection::Evaluate) + GetSectionMicroseconds(EACFCameraProfileSection::PreEvaluate)
		+ GetSectionMicroseconds(EACFCameraProfileSection::BackgroundUpstream) + GetSectionMicroseconds(EACFCameraProfileSection::OutputGuard);
}

// -------------------------------------------------------------------------------------------------
// Statistics

FACFCameraProfileStats FACFCameraProfileStats::Compute(TArray<double>& Values)
{
	FACFCameraProfileStats Stats;
	Stats.Samples = Values.Num();
	if (Values.IsEmpty())
	{
		return Stats;
	}
	Values.Sort();
	double Sum = 0.0;
	for (const double V : Values)
	{
		Sum += V;
	}
	Stats.Mean = Sum / Values.Num();
	const int32 N = Values.Num();
	Stats.Median = (N % 2) ? Values[N / 2] : 0.5 * (Values[N / 2 - 1] + Values[N / 2]);
	// Nearest-rank P95.
	const int32 Rank = FMath::Clamp(FMath::CeilToInt(0.95 * N), 1, N);
	Stats.P95 = Values[Rank - 1];
	Stats.Max = Values.Last();
	return Stats;
}

FString FACFCameraProfileStats::ToString(int32 Decimals) const
{
	return FString::Printf(TEXT("mean %.*f / median %.*f / p95 %.*f / max %.*f (n=%d)"), Decimals, Mean, Decimals, Median, Decimals, P95, Decimals, Max, Samples);
}

FACFCameraProfileSummary FACFCameraProfileSummary::Summarize(TConstArrayView<const FACFCameraProfileFrame*> Frames)
{
	FACFCameraProfileSummary Out;
	Out.NumFrames = Frames.Num();
	TArray<double> Values;
	Values.Reserve(Frames.Num());
	auto Series = [&](TFunctionRef<double(const FACFCameraProfileFrame&)> Get, bool bOnlyEvaluated = false)
	{
		Values.Reset();
		for (const FACFCameraProfileFrame* Frame : Frames)
		{
			if (!bOnlyEvaluated || Frame->NumEvaluations > 0)
			{
				Values.Add(Get(*Frame));
			}
		}
		return FACFCameraProfileStats::Compute(Values);
	};

	for (const FACFCameraProfileFrame* Frame : Frames)
	{
		Out.NumEvaluations += Frame->NumEvaluations;
		Out.NumBackgroundFrames += Frame->bBackground ? 1 : 0;
		Out.Allocations += Frame->Allocations;
		Out.AllocatedBytes += Frame->AllocatedBytes;
		Out.FramesWithAllocations += Frame->Allocations > 0 ? 1 : 0;
		Out.DroppedNodeSamples += Frame->DroppedNodeSamples;
		for (int32 S = 0; S <= FACFCameraProfileFrame::NumSections; ++S)
		{
			Out.AllocationsBySection[S] += Frame->AllocationsBySection[S];
		}
	}
	Out.TotalUs = Series([](const FACFCameraProfileFrame& F) { return F.GetTotalMicroseconds(); });
	for (int32 S = 0; S < FACFCameraProfileFrame::NumSections; ++S)
	{
		Out.SectionUs[S] = Series([S](const FACFCameraProfileFrame& F) { return F.GetSectionMicroseconds(static_cast<EACFCameraProfileSection>(S)); });
	}
	Out.QueriesPerFrame = Series([](const FACFCameraProfileFrame& F) { return static_cast<double>(F.GetFrameQueries()); });
	// Per evaluation: the evaluation queries of a frame divided by its evaluations (frames without one are skipped).
	Out.QueriesPerEvaluation = Series([](const FACFCameraProfileFrame& F) { return static_cast<double>(F.GetEvaluationQueries()) / FMath::Max<int32>(F.NumEvaluations, 1); }, true);
	for (int32 P = 0; P < FACFCameraProfileFrame::NumPurposes; ++P)
	{
		Out.QueriesByPurpose[P] = Series([P](const FACFCameraProfileFrame& F) { return static_cast<double>(F.GetQueries(static_cast<EACFCameraQueryPurpose>(P))); });
	}
	for (int32 S = 0; S < FACFCameraProfileFrame::NumShapes; ++S)
	{
		Out.QueriesByShape[S] = Series([S](const FACFCameraProfileFrame& F) { return static_cast<double>(F.GetQueries(static_cast<EACFCameraQueryShape>(S))); });
	}

	// Nodes: per frame, the sum of the runs of each node class (a class can run twice while two rigs blend).
	TArray<FName> Names;
	for (const FACFCameraProfileFrame* Frame : Frames)
	{
		for (int32 I = 0; I < Frame->NumNodeSamples; ++I)
		{
			Names.AddUnique(Frame->Nodes[I].NodeName);
		}
	}
	for (const FName Name : Names)
	{
		Values.Reset();
		for (const FACFCameraProfileFrame* Frame : Frames)
		{
			uint64 Cycles = 0;
			bool bRan = false;
			for (int32 I = 0; I < Frame->NumNodeSamples; ++I)
			{
				if (Frame->Nodes[I].NodeName == Name)
				{
					Cycles += Frame->Nodes[I].Cycles;
					bRan = true;
				}
			}
			if (bRan)
			{
				Values.Add(FPlatformTime::ToMilliseconds64(Cycles) * 1000.0);
			}
		}
		Out.NodeUs.Emplace(Name, FACFCameraProfileStats::Compute(Values));
	}
	Out.NodeUs.Sort([](const TPair<FName, FACFCameraProfileStats>& A, const TPair<FName, FACFCameraProfileStats>& B) { return A.Value.Mean > B.Value.Mean; });
	return Out;
}

FString FACFCameraProfileSummary::ToString() const
{
	FString Out = FString::Printf(TEXT("frames %d, evaluations %d, background frames %d\n"), NumFrames, NumEvaluations, NumBackgroundFrames);
	Out += FString::Printf(TEXT("  ACF total us        %s\n"), *TotalUs.ToString());
	for (int32 S = 0; S < FACFCameraProfileFrame::NumSections; ++S)
	{
		if (SectionUs[S].Max > 0.0)
		{
			Out += FString::Printf(TEXT("  %-19s %s\n"), LexToString(static_cast<EACFCameraProfileSection>(S)), *SectionUs[S].ToString());
		}
	}
	for (const TPair<FName, FACFCameraProfileStats>& Node : NodeUs)
	{
		Out += FString::Printf(TEXT("  node %-30s %s\n"), *Node.Key.ToString(), *Node.Value.ToString());
	}
	Out += FString::Printf(TEXT("  queries / frame     %s\n"), *QueriesPerFrame.ToString());
	Out += FString::Printf(TEXT("  queries / eval      %s\n"), *QueriesPerEvaluation.ToString());
	for (int32 P = 0; P < FACFCameraProfileFrame::NumPurposes; ++P)
	{
		if (QueriesByPurpose[P].Max > 0.0)
		{
			Out += FString::Printf(TEXT("  queries %-16s %s\n"), LexToString(static_cast<EACFCameraQueryPurpose>(P)), *QueriesByPurpose[P].ToString());
		}
	}
	Out += FString::Printf(TEXT("  allocations %llu (%llu bytes) in %d frames"), Allocations, AllocatedBytes, FramesWithAllocations);
	if (DroppedNodeSamples > 0)
	{
		Out += FString::Printf(TEXT(", dropped node samples %d"), DroppedNodeSamples);
	}
	return Out;
}

#if ACF_WITH_PROFILER

#include "HAL/IConsoleManager.h"
#include "HAL/MemoryBase.h"
#include "HAL/PlatformStackWalk.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FACFCameraProfiler* FACFCameraProfiler::GActive = nullptr;

// -------------------------------------------------------------------------------------------------
// Recorder

void FACFCameraProfiler::Start(int32 InCapacity)
{
	if (IsRecording())
	{
		return;
	}
	Frames.SetNum(FMath::Clamp(InCapacity, 16, 65536));
	Head = -1;
	Count = 0;
	CurrentFrame = nullptr;
}

void FACFCameraProfiler::Stop()
{
	check(GActive != this); // never stopped from inside its own scope
	Frames.Empty();
	Head = -1;
	Count = 0;
	CurrentFrame = nullptr;
}

void FACFCameraProfiler::Clear()
{
	Head = -1;
	Count = 0;
	CurrentFrame = nullptr;
}

const FACFCameraProfileFrame& FACFCameraProfiler::GetFrame(int32 IndexFromOldest) const
{
	check(IndexFromOldest >= 0 && IndexFromOldest < Count);
	const int32 Oldest = (Head - Count + 1 + Frames.Num()) % Frames.Num();
	return Frames[(Oldest + IndexFromOldest) % Frames.Num()];
}

void FACFCameraProfiler::GetFrames(TArray<const FACFCameraProfileFrame*>& Out) const
{
	Out.Reset(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		Out.Add(&GetFrame(I));
	}
}

FACFCameraProfileFrame* FACFCameraProfiler::BeginFrame(uint64 FrameNumber)
{
	if (Count > 0 && Frames[Head].FrameNumber == FrameNumber)
	{
		return &Frames[Head];
	}
	Head = (Head + 1) % Frames.Num();
	Count = FMath::Min(Count + 1, Frames.Num());
	FACFCameraProfileFrame& Frame = Frames[Head];
	Frame = FACFCameraProfileFrame(); // fixed size: no heap
	Frame.FrameNumber = FrameNumber;
	return &Frame;
}

FACFCameraProfiler::FActiveScope::FActiveScope(FACFCameraProfiler* Profiler, uint64 FrameNumber, float DeltaTime, bool bIsEvaluation, bool bBackground)
	: Previous(GActive), bSet(Profiler && Profiler->IsRecording())
{
	if (!bSet)
	{
		return;
	}
	check(IsInGameThread());
	FACFCameraProfileFrame* Frame = Profiler->BeginFrame(FrameNumber);
	if (bIsEvaluation)
	{
		Frame->NumEvaluations = static_cast<uint8>(FMath::Min<int32>(Frame->NumEvaluations + 1, MAX_uint8));
		Frame->DeltaTime = DeltaTime;
		Frame->bBackground |= bBackground;
	}
	Profiler->CurrentFrame = Frame;
	GActive = Profiler;
}

FACFCameraProfiler::FActiveScope::~FActiveScope()
{
	if (bSet)
	{
		GActive = Previous;
	}
}

void FACFCameraProfiler::AddSection(EACFCameraProfileSection Section, uint64 Cycles)
{
	if (CurrentFrame)
	{
		CurrentFrame->SectionCycles[static_cast<int32>(Section)] += Cycles;
	}
}

void FACFCameraProfiler::AddQuery(EACFCameraQueryShape Shape)
{
	if (CurrentFrame)
	{
		uint16& Counter = CurrentFrame->Queries[static_cast<int32>(Purpose)][static_cast<int32>(Shape)];
		Counter = static_cast<uint16>(FMath::Min<int32>(Counter + 1, MAX_uint16));
	}
}

void FACFCameraProfiler::AddNode(FName NodeName, uint8 Stage, uint64 Cycles)
{
	if (!CurrentFrame || Purpose == EACFCameraQueryPurpose::ShotProbe)
	{
		return; // probe rigs are measured as the ShotProbe section, not as gameplay nodes
	}
	if (CurrentFrame->NumNodeSamples >= FACFCameraProfileFrame::MaxNodeSamples)
	{
		++CurrentFrame->DroppedNodeSamples;
		return;
	}
	FACFCameraProfileNodeSample& Sample = CurrentFrame->Nodes[CurrentFrame->NumNodeSamples++];
	Sample.NodeName = NodeName;
	Sample.Stage = Stage;
	Sample.Cycles = Cycles;
}

void FACFCameraProfiler::AddAllocation(SIZE_T Size)
{
	if (!CurrentFrame)
	{
		return;
	}
	++CurrentFrame->Allocations;
	CurrentFrame->AllocatedBytes += Size;
	const int32 Slot = CurrentSection >= 0 ? CurrentSection : FACFCameraProfileFrame::NumSections;
	uint16& Counter = CurrentFrame->AllocationsBySection[Slot];
	Counter = static_cast<uint16>(FMath::Min<int32>(Counter + 1, MAX_uint16));
}

// -------------------------------------------------------------------------------------------------
// Allocation counter

namespace ACFAllocationCounter
{
	static TAtomic<bool> GCounting(false);
	static bool GInstalled = false;
	static bool GInHook = false; // game thread only (the hook only runs there)
	static FACFCameraAllocationCounter::FSite GSites[FACFCameraAllocationCounter::MaxSites];
	static int32 GNumSites = 0;
	static uint32 GOverflow = 0;

	static void OnAllocation(SIZE_T Size)
	{
		if (!GCounting.Load(EMemoryOrder::Relaxed) || !IsInGameThread() || GInHook)
		{
			return;
		}
		FACFCameraProfiler* Profiler = FACFCameraProfiler::GetActive();
		if (!Profiler)
		{
			return;
		}
		TGuardValue<bool> Guard(GInHook, true);
		Profiler->AddAllocation(Size);

		uint64 Stack[FACFCameraAllocationCounter::MaxDepth];
		const int32 Depth = static_cast<int32>(FPlatformStackWalk::CaptureStackBackTrace(Stack, FACFCameraAllocationCounter::MaxDepth));
		uint64 Hash = 1469598103934665603ull;
		for (int32 I = 0; I < Depth; ++I)
		{
			Hash = (Hash ^ Stack[I]) * 1099511628211ull;
		}
		for (int32 I = 0; I < GNumSites; ++I)
		{
			if (GSites[I].Hash == Hash)
			{
				++GSites[I].Count;
				GSites[I].Bytes += Size;
				return;
			}
		}
		if (GNumSites >= FACFCameraAllocationCounter::MaxSites)
		{
			++GOverflow;
			return;
		}
		FACFCameraAllocationCounter::FSite& Site = GSites[GNumSites++];
		Site.Hash = Hash;
		Site.Count = 1;
		Site.Bytes = Size;
		Site.Section = Profiler->GetCurrentSection();
		Site.Depth = Depth;
		FMemory::Memcpy(Site.Stack, Stack, sizeof(uint64) * Depth);
	}

	/** Forwards everything to the allocator it wraps; notes allocations (Malloc / Realloc growth) for the counter. */
	class FCountingMalloc final : public FMalloc
	{
	public:
		explicit FCountingMalloc(FMalloc* InInner) : Inner(InInner) {}

		virtual void* Malloc(SIZE_T Count, uint32 Alignment) override { OnAllocation(Count); return Inner->Malloc(Count, Alignment); }
		virtual void* TryMalloc(SIZE_T Count, uint32 Alignment) override { OnAllocation(Count); return Inner->TryMalloc(Count, Alignment); }
		virtual void* Realloc(void* Original, SIZE_T Count, uint32 Alignment) override { if (Count > 0) { OnAllocation(Count); } return Inner->Realloc(Original, Count, Alignment); }
		virtual void* TryRealloc(void* Original, SIZE_T Count, uint32 Alignment) override { if (Count > 0) { OnAllocation(Count); } return Inner->TryRealloc(Original, Count, Alignment); }
		virtual void Free(void* Original) override { Inner->Free(Original); }
		virtual void* MallocZeroed(SIZE_T Count, uint32 Alignment) override { OnAllocation(Count); return Inner->MallocZeroed(Count, Alignment); }
		virtual void* TryMallocZeroed(SIZE_T Count, uint32 Alignment) override { OnAllocation(Count); return Inner->TryMallocZeroed(Count, Alignment); }
		virtual SIZE_T QuantizeSize(SIZE_T Count, uint32 Alignment) override { return Inner->QuantizeSize(Count, Alignment); }
		virtual bool GetAllocationSize(void* Original, SIZE_T& SizeOut) override { return Inner->GetAllocationSize(Original, SizeOut); }
		virtual void Trim(bool bTrimThreadCaches) override { Inner->Trim(bTrimThreadCaches); }
		virtual void SetupTLSCachesOnCurrentThread() override { Inner->SetupTLSCachesOnCurrentThread(); }
		virtual void MarkTLSCachesAsUsedOnCurrentThread() override { Inner->MarkTLSCachesAsUsedOnCurrentThread(); }
		virtual void MarkTLSCachesAsUnusedOnCurrentThread() override { Inner->MarkTLSCachesAsUnusedOnCurrentThread(); }
		virtual void ClearAndDisableTLSCachesOnCurrentThread() override { Inner->ClearAndDisableTLSCachesOnCurrentThread(); }
		virtual void InitializeStatsMetadata() override { Inner->InitializeStatsMetadata(); }
#if UE_ALLOW_EXEC_COMMANDS
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return Inner->Exec(InWorld, Cmd, Ar); }
#endif
		virtual void UpdateStats() override { Inner->UpdateStats(); }
		virtual void GetAllocatorStats(FGenericMemoryStats& OutStats) override { Inner->GetAllocatorStats(OutStats); }
		virtual void DumpAllocatorStats(FOutputDevice& Ar) override { Inner->DumpAllocatorStats(Ar); }
		virtual bool IsInternallyThreadSafe() const override { return Inner->IsInternallyThreadSafe(); }
		virtual bool ValidateHeap() override { return Inner->ValidateHeap(); }
		virtual const TCHAR* GetDescriptiveName() override { return Inner->GetDescriptiveName(); }
		virtual void OnMallocInitialized() override { Inner->OnMallocInitialized(); }
		virtual void OnPreFork() override { Inner->OnPreFork(); }
		virtual void OnPostFork() override { Inner->OnPostFork(); }
		virtual uint64 GetImmediatelyFreeableCachedMemorySize() const override { return Inner->GetImmediatelyFreeableCachedMemorySize(); }
		virtual uint64 GetTotalFreeCachedMemorySize() const override { return Inner->GetTotalFreeCachedMemorySize(); }

	private:
		FMalloc* Inner;
	};
}

bool FACFCameraAllocationCounter::Install()
{
	using namespace ACFAllocationCounter;
	check(IsInGameThread());
	if (GInstalled)
	{
		return true;
	}
	if (PLATFORM_USES_FIXED_GMalloc_CLASS)
	{
		UE_LOG(LogActionCamera, Warning, TEXT("ACF allocation counter: not available (fixed allocator class)."));
		return false;
	}
	FPlatformStackWalk::InitStackWalking(); // before the hook can capture a stack (initialization may allocate)
	// Same pattern as FMemory::EnablePoisonTests: swap the allocator pointer atomically, keep the proxy forever.
	while (true)
	{
		FMalloc* Current = UE::Private::GMalloc;
		FMalloc* Proxy = new FCountingMalloc(Current);
		if (FPlatformAtomics::InterlockedCompareExchangePointer((void**)&UE::Private::GMalloc, Proxy, Current) == Current)
		{
			GInstalled = true;
			UE_LOG(LogActionCamera, Log, TEXT("ACF allocation counter installed over %s."), Current->GetDescriptiveName());
			return true;
		}
		delete Proxy;
	}
}

bool FACFCameraAllocationCounter::IsInstalled()
{
	return ACFAllocationCounter::GInstalled;
}

void FACFCameraAllocationCounter::SetCounting(bool bEnabled)
{
	ACFAllocationCounter::GCounting = bEnabled && ACFAllocationCounter::GInstalled;
}

bool FACFCameraAllocationCounter::IsCounting()
{
	return ACFAllocationCounter::GCounting.Load();
}

void FACFCameraAllocationCounter::GetSites(TArray<FSite>& Out, uint32& OutOverflowCount)
{
	Out.Reset();
	Out.Append(ACFAllocationCounter::GSites, ACFAllocationCounter::GNumSites);
	OutOverflowCount = ACFAllocationCounter::GOverflow;
}

void FACFCameraAllocationCounter::ResetSites()
{
	ACFAllocationCounter::GNumSites = 0;
	ACFAllocationCounter::GOverflow = 0;
}

FString FACFCameraAllocationCounter::DescribeSite(const FSite& Site, int32 MaxFrames)
{
	FString Out;
	int32 Written = 0;
	for (int32 I = 0; I < Site.Depth && Written < MaxFrames; ++I)
	{
		ANSICHAR Buffer[1024];
		Buffer[0] = 0;
		FPlatformStackWalk::ProgramCounterToHumanReadableString(I, Site.Stack[I], Buffer, sizeof(Buffer));
		const FString Line = ANSI_TO_TCHAR(Buffer);
		// Skip the counter / allocator plumbing and frames without symbols (engine PDBs are optional).
		if (Line.Contains(TEXT("UnknownFunction")) || Line.Contains(TEXT("ACFAllocationCounter")) || Line.Contains(TEXT("FMemory")) || Line.Contains(TEXT("FMalloc"))
			|| Line.Contains(TEXT("CaptureStackBackTrace")) || Line.Contains(TEXT("operator new")))
		{
			continue;
		}
		Out += FString::Printf(TEXT("      %s\n"), *Line);
		++Written;
	}
	return Out;
}

// -------------------------------------------------------------------------------------------------
// Console control (development builds only)

namespace ACFProfilerConsole
{
	static TAutoConsoleVariable<int32> CVarProfile(TEXT("ACF.Profile"), 0,
		TEXT("Records ACF camera evaluation timings, collision query counts and (with ACF.Profile.Allocations) allocations per local player.\n")
		TEXT("0 = off (recording buffers are freed), 1 = on. Reports: ACF.Profile.Report [Name]."), ECVF_Default);
	static TAutoConsoleVariable<int32> CVarCapacity(TEXT("ACF.Profile.Capacity"), FACFCameraProfiler::DefaultCapacity,
		TEXT("Frames kept per local player while ACF.Profile is on (applied when recording starts)."), ECVF_Default);
}

bool ACFCameraProfiling::IsRequestedByConsole()
{
	return ACFProfilerConsole::CVarProfile.GetValueOnGameThread() != 0;
}

int32 ACFCameraProfiling::GetRequestedCapacity()
{
	return ACFProfilerConsole::CVarCapacity.GetValueOnGameThread();
}

FString ACFCameraProfiling::BuildCsv(const FACFCameraProfiler& Profiler)
{
	FString Csv = TEXT("Frame,DeltaTime,Evaluations,Background,TotalUs");
	for (int32 S = 0; S < FACFCameraProfileFrame::NumSections; ++S)
	{
		Csv += FString::Printf(TEXT(",%sUs"), LexToString(static_cast<EACFCameraProfileSection>(S)));
	}
	for (int32 P = 0; P < FACFCameraProfileFrame::NumPurposes; ++P)
	{
		for (int32 S = 0; S < FACFCameraProfileFrame::NumShapes; ++S)
		{
			Csv += FString::Printf(TEXT(",Q_%s_%s"), LexToString(static_cast<EACFCameraQueryPurpose>(P)), LexToString(static_cast<EACFCameraQueryShape>(S)));
		}
	}
	Csv += TEXT(",Allocations,AllocatedBytes,Nodes\n");
	for (int32 I = 0; I < Profiler.Num(); ++I)
	{
		const FACFCameraProfileFrame& F = Profiler.GetFrame(I);
		Csv += FString::Printf(TEXT("%llu,%.6f,%d,%d,%.3f"), F.FrameNumber, F.DeltaTime, F.NumEvaluations, F.bBackground ? 1 : 0, F.GetTotalMicroseconds());
		for (int32 S = 0; S < FACFCameraProfileFrame::NumSections; ++S)
		{
			Csv += FString::Printf(TEXT(",%.3f"), F.GetSectionMicroseconds(static_cast<EACFCameraProfileSection>(S)));
		}
		for (int32 P = 0; P < FACFCameraProfileFrame::NumPurposes; ++P)
		{
			for (int32 S = 0; S < FACFCameraProfileFrame::NumShapes; ++S)
			{
				Csv += FString::Printf(TEXT(",%d"), F.Queries[P][S]);
			}
		}
		Csv += FString::Printf(TEXT(",%u,%llu,"), F.Allocations, F.AllocatedBytes);
		for (int32 N = 0; N < F.NumNodeSamples; ++N)
		{
			Csv += FString::Printf(TEXT("%s%s:%.3f"), N > 0 ? TEXT(";") : TEXT(""), *F.Nodes[N].NodeName.ToString(), FPlatformTime::ToMilliseconds64(F.Nodes[N].Cycles) * 1000.0);
		}
		Csv += TEXT("\n");
	}
	return Csv;
}

#endif // ACF_WITH_PROFILER
