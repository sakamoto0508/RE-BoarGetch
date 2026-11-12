#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

/**
 * ACF camera profiler (Phase 6B / B-1). Measures what ACF itself costs, per local player:
 *  - CPU time of the evaluation stages and of every camera node (FPlatformTime cycles),
 *  - the collision queries ACF issued (by purpose and shape),
 *  - optionally, the heap allocations made on the game thread while ACF code runs.
 *
 * Every measured scope also emits an Unreal Insights CPU event (TRACE_CPUPROFILER_*), so the same points can be
 * seen in Insights (`-trace=cpu`). Insights events exist whenever tracing is compiled in; the ACF recorder below is
 * compiled out of Shipping (ACF_WITH_PROFILER == 0) and, when compiled in but not recording, costs one pointer
 * test per scope.
 *
 * Ownership: one FACFCameraProfiler per UACFCameraDirector (= per local player). Recording uses a ring buffer that
 * is allocated when recording starts, never during an evaluation, and the recorder holds no UObject / world pointer
 * (node names are FNames), so nothing outlives a world.
 *
 * Phase 7A (Camera Profiler / Debugger) is meant to read FACFCameraProfileFrame / Summarize; no UI is provided here.
 */

#ifndef ACF_WITH_PROFILER
#define ACF_WITH_PROFILER (!UE_BUILD_SHIPPING)
#endif

/** Measured stages. Nesting: Evaluate contains SequenceTick..ShotProbe; PostBlend contains Deocclude / FramingRecovery. */
enum class EACFCameraProfileSection : uint8
{
	/** UACFCameraDirector::Evaluate (only evaluations that run; cached repeats of the same frame are not counted). */
	Evaluate,
	/** Camera sequences (finishers) advancing. Inside Evaluate. */
	SequenceTick,
	/** Lost-instigator pruning, priority resolution, transition start / pending entry blend. Inside Evaluate. */
	ResolveRequests,
	/** Body + Aim + Framing stages of every rig evaluated this frame (target and live blend source). Inside Evaluate. */
	RigEvaluate,
	/** Effect stack evaluation + pre / post collision application. Inside Evaluate. */
	Effects,
	/** PostBlend stage (Collision node with its constraint solver). Inside Evaluate. */
	PostBlend,
	/** Constraint solver: deoccluder (visibility checks and candidate search). Inside PostBlend. */
	Deocclude,
	/** Constraint solver: framing recovery. Inside PostBlend. */
	FramingRecovery,
	/** Shot feasibility probe of camera sequences (throw-away rig evaluation). Inside SequenceTick. */
	ShotProbe,
	/** OnPreEvaluate broadcast (the Sequencer module's state refresh). Before Evaluate, not nested. */
	PreEvaluate,
	/** Background evaluation only: resolving the gameplay view target's camera (CalcCamera). Not nested. */
	BackgroundUpstream,
	/** Final output guard sweep (after the PCM weight blend and shakes; once per PCM view target update). Not nested. */
	OutputGuard,
	Num
};

/** Why ACF issued a collision query. */
enum class EACFCameraQueryPurpose : uint8
{
	/** Collision node: pivot -> camera sweep (and its retry when the pivot starts inside geometry). */
	Collision,
	/** Deoccluder: visibility of the goal correction (and the base pose while correcting). */
	DeoccluderCheck,
	/** Deoccluder: candidate search (only on a confirmed occlusion, throttled). */
	DeoccluderSearch,
	/** Final output guard. */
	OutputGuard,
	/** Camera sequence shot probe (sweep of the probed rig + line of sight to attacker / target). */
	ShotProbe,
	Num
};

enum class EACFCameraQueryShape : uint8
{
	Sweep,
	Line,
	Num
};

struct FACFCameraProfileNodeSample
{
	/** Node class name (no UObject pointer is kept). */
	FName NodeName;
	/** EACFCameraNodeStage. */
	uint8 Stage = 0;
	uint64 Cycles = 0;
};

/** Everything recorded for one engine frame of one local player. Plain data, fixed size (no heap). */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraProfileFrame
{
	static constexpr int32 MaxNodeSamples = 24;
	static constexpr int32 NumSections = static_cast<int32>(EACFCameraProfileSection::Num);
	static constexpr int32 NumPurposes = static_cast<int32>(EACFCameraQueryPurpose::Num);
	static constexpr int32 NumShapes = static_cast<int32>(EACFCameraQueryShape::Num);

	uint64 FrameNumber = 0;
	float DeltaTime = 0.f;
	/** Director evaluations that ran in this frame (normally 1; 2 when a zero-delta evaluation was upgraded). */
	uint8 NumEvaluations = 0;
	/** The evaluation was a background one (only external view targets were on screen, e.g. a Sequencer camera cut). */
	bool bBackground = false;
	uint64 SectionCycles[NumSections] = {};
	uint16 Queries[NumPurposes][NumShapes] = {};
	/** Game-thread heap allocations while ACF code ran (only when the allocation counter is installed and on). */
	uint32 Allocations = 0;
	uint64 AllocatedBytes = 0;
	uint16 AllocationsBySection[NumSections + 1] = {}; // last slot: outside any section
	int32 NumNodeSamples = 0;
	/** Node samples that did not fit (more than MaxNodeSamples node runs in one frame). */
	int32 DroppedNodeSamples = 0;
	FACFCameraProfileNodeSample Nodes[MaxNodeSamples];

	double GetSectionMicroseconds(EACFCameraProfileSection Section) const;
	int32 GetQueries(EACFCameraQueryPurpose Purpose) const;
	int32 GetQueries(EACFCameraQueryShape Shape) const;
	/** Queries issued inside evaluations (everything except the output guard). */
	int32 GetEvaluationQueries() const;
	/** All ACF queries of the frame (evaluation + output guard). */
	int32 GetFrameQueries() const;
	/** ACF's own time in the frame: Evaluate + PreEvaluate + BackgroundUpstream + OutputGuard (not nested). */
	double GetTotalMicroseconds() const;
};

/** Mean / median / P95 / max of a series. */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraProfileStats
{
	int32 Samples = 0;
	double Mean = 0.0;
	double Median = 0.0;
	double P95 = 0.0;
	double Max = 0.0;

	/** Sorts Values in place. */
	static FACFCameraProfileStats Compute(TArray<double>& Values);
	FString ToString(int32 Decimals = 2) const;
};

/** Aggregate of a set of frames (allocates: call it outside evaluations, e.g. from a report). */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraProfileSummary
{
	int32 NumFrames = 0;
	int32 NumEvaluations = 0;
	int32 NumBackgroundFrames = 0;
	FACFCameraProfileStats TotalUs;
	FACFCameraProfileStats SectionUs[FACFCameraProfileFrame::NumSections];
	FACFCameraProfileStats QueriesPerFrame;
	FACFCameraProfileStats QueriesPerEvaluation;
	FACFCameraProfileStats QueriesByPurpose[FACFCameraProfileFrame::NumPurposes];
	FACFCameraProfileStats QueriesByShape[FACFCameraProfileFrame::NumShapes];
	/** Per node class: time per frame (sum of its runs in the frame), over the frames where it ran. */
	TArray<TPair<FName, FACFCameraProfileStats>> NodeUs;
	uint64 Allocations = 0;
	uint64 AllocatedBytes = 0;
	int32 FramesWithAllocations = 0;
	uint64 AllocationsBySection[FACFCameraProfileFrame::NumSections + 1] = {};
	int32 DroppedNodeSamples = 0;

	static FACFCameraProfileSummary Summarize(TConstArrayView<const FACFCameraProfileFrame*> Frames);
	FString ToString() const;
};

ACTIONCAMERAFRAMEWORK_API const TCHAR* LexToString(EACFCameraProfileSection Section);
ACTIONCAMERAFRAMEWORK_API const TCHAR* LexToString(EACFCameraQueryPurpose Purpose);
ACTIONCAMERAFRAMEWORK_API const TCHAR* LexToString(EACFCameraQueryShape Shape);

#if ACF_WITH_PROFILER

/** Per-director recorder (see file comment). Game thread only. */
class ACTIONCAMERAFRAMEWORK_API FACFCameraProfiler
{
public:
	static constexpr int32 DefaultCapacity = 1024;

	/** Allocates the ring buffer (the only allocation of the recorder) and starts recording. Keeps old frames if already recording. */
	void Start(int32 Capacity = DefaultCapacity);
	/** Stops recording and frees the buffer. */
	void Stop();
	/** Drops the recorded frames, keeps recording. */
	void Clear();
	bool IsRecording() const { return Frames.Num() > 0; }
	int32 Capacity() const { return Frames.Num(); }

	/** Recorded frames, oldest first. */
	int32 Num() const { return Count; }
	const FACFCameraProfileFrame& GetFrame(int32 IndexFromOldest) const;
	void GetFrames(TArray<const FACFCameraProfileFrame*>& Out) const;
	/** Most recent frame, or null. */
	const FACFCameraProfileFrame* GetLatestFrame() const { return Count > 0 ? &Frames[Head] : nullptr; }

	/** The profiler of the ACF code currently running on the game thread, or null (not recording / outside ACF). */
	static FACFCameraProfiler* GetActive() { return GActive; }

	/**
	 * Makes Profiler the active one for the scope and selects the record of Frame (records of the same frame number
	 * are shared, so the pre-evaluate, the evaluation and the guard of one frame land in one record). Null = no-op.
	 */
	struct ACTIONCAMERAFRAMEWORK_API FActiveScope
	{
		FActiveScope(FACFCameraProfiler* Profiler, uint64 FrameNumber, float DeltaTime, bool bIsEvaluation, bool bBackground);
		~FActiveScope();
		FACFCameraProfiler* Previous;
		bool bSet;
	};

	/** Times a section of the active profiler (inclusive). */
	struct FSectionScope
	{
		explicit FSectionScope(EACFCameraProfileSection InSection)
			: Profiler(GActive), Section(InSection)
		{
			if (Profiler)
			{
				PreviousSection = Profiler->CurrentSection;
				Profiler->CurrentSection = static_cast<int8>(Section);
				Start = FPlatformTime::Cycles64();
			}
		}
		~FSectionScope()
		{
			if (Profiler)
			{
				Profiler->AddSection(Section, FPlatformTime::Cycles64() - Start);
				Profiler->CurrentSection = PreviousSection;
			}
		}
		FACFCameraProfiler* Profiler;
		EACFCameraProfileSection Section;
		uint64 Start = 0;
		int8 PreviousSection = -1;
	};

	/** Sets the purpose of the queries counted inside the scope. */
	struct FPurposeScope
	{
		explicit FPurposeScope(EACFCameraQueryPurpose InPurpose)
			: Profiler(GActive)
		{
			if (Profiler)
			{
				Previous = Profiler->Purpose;
				Profiler->Purpose = InPurpose;
			}
		}
		~FPurposeScope()
		{
			if (Profiler)
			{
				Profiler->Purpose = Previous;
			}
		}
		FACFCameraProfiler* Profiler;
		EACFCameraQueryPurpose Previous = EACFCameraQueryPurpose::Collision;
	};

	static void CountQuery(EACFCameraQueryShape Shape)
	{
		if (FACFCameraProfiler* Profiler = GActive)
		{
			Profiler->AddQuery(Shape);
		}
	}

	/** Called by the rig for every node run (no-op while probing a shot). */
	void AddNode(FName NodeName, uint8 Stage, uint64 Cycles);

	/** Called by the allocation counter (game thread, inside an active scope). */
	void AddAllocation(SIZE_T Size);

	EACFCameraQueryPurpose GetPurpose() const { return Purpose; }
	int8 GetCurrentSection() const { return CurrentSection; }

private:
	void AddSection(EACFCameraProfileSection Section, uint64 Cycles);
	void AddQuery(EACFCameraQueryShape Shape);
	FACFCameraProfileFrame* BeginFrame(uint64 FrameNumber);

	TArray<FACFCameraProfileFrame> Frames;
	int32 Head = -1;
	int32 Count = 0;
	FACFCameraProfileFrame* CurrentFrame = nullptr;
	EACFCameraQueryPurpose Purpose = EACFCameraQueryPurpose::Collision;
	int8 CurrentSection = -1;

	static FACFCameraProfiler* GActive;
};

/**
 * Counts game-thread heap allocations made while an ACF profiler scope is active, by wrapping the engine allocator
 * (GMalloc) with a forwarding proxy - the same way the engine installs its own debug proxies at runtime. Installed
 * only on request (ACF.Profile.Allocations 1 / tests) and never removed (turning it off stops the counting; the
 * forwarding stays). Other threads and code outside ACF are forwarded without being counted.
 */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraAllocationCounter
{
	static constexpr int32 MaxSites = 64;
	static constexpr int32 MaxDepth = 20;

	/** One distinct allocation call stack seen while counting. */
	struct FSite
	{
		uint64 Hash = 0;
		uint32 Count = 0;
		uint64 Bytes = 0;
		int8 Section = -1;
		int32 Depth = 0;
		uint64 Stack[MaxDepth] = {};
	};

	/** Installs the proxy once. False if the platform uses a fixed allocator class. */
	static bool Install();
	static bool IsInstalled();
	static void SetCounting(bool bEnabled);
	static bool IsCounting();

	/** Call stacks captured while counting (deduplicated, up to MaxSites; later new sites are counted as overflow). */
	static void GetSites(TArray<FSite>& Out, uint32& OutOverflowCount);
	static void ResetSites();
	/** Symbolizes a site (allocates; call with counting off). Skips the frames of the counter itself. */
	static FString DescribeSite(const FSite& Site, int32 MaxFrames = 10);
};

namespace ACFCameraProfiling
{
	/** Console control: ACF.Profile (0 / 1) and ACF.Profile.Capacity. */
	ACTIONCAMERAFRAMEWORK_API bool IsRequestedByConsole();
	ACTIONCAMERAFRAMEWORK_API int32 GetRequestedCapacity();
	/** One line per recorded frame (sections in microseconds, queries, allocations, node samples). Allocates. */
	ACTIONCAMERAFRAMEWORK_API FString BuildCsv(const FACFCameraProfiler& Profiler);
}

#define ACF_PROFILE_SCOPE(Section) \
	TRACE_CPUPROFILER_EVENT_SCOPE(ACF_##Section); \
	FACFCameraProfiler::FSectionScope UE_JOIN(ACFProfileSection_, __LINE__)(EACFCameraProfileSection::Section)
#define ACF_PROFILE_PURPOSE(PurposeName) \
	FACFCameraProfiler::FPurposeScope UE_JOIN(ACFProfilePurpose_, __LINE__)(EACFCameraQueryPurpose::PurposeName)
#define ACF_PROFILE_QUERY(ShapeName) FACFCameraProfiler::CountQuery(EACFCameraQueryShape::ShapeName)

#else

#define ACF_PROFILE_SCOPE(Section) TRACE_CPUPROFILER_EVENT_SCOPE(ACF_##Section)
#define ACF_PROFILE_PURPOSE(PurposeName)
#define ACF_PROFILE_QUERY(ShapeName)

#endif // ACF_WITH_PROFILER
