#pragma once

#include "CoreMinimal.h"
#include "ACFCameraDirector.h"
#include "ACFCameraProfiler.h"
#include "ACFCameraTypes.h"

class APlayerController;
class FACFCameraTuningPanelModel;
class UCanvas;

/**
 * Phase 7A-4: what the Profiler / Debugger / Composition views of the tuning panel show (README 27 章). No Slate.
 *
 * It OBSERVES the target of the tuning panel (same PIE World / local player / request, so tuning and diagnostics always
 * look at the same camera) through read-only APIs: the B-1 profiler ring buffer of that player's director, the last
 * evaluation result / rig debug / solver report / health / effects / transition of the director, the subsystem's output
 * diagnostics and the player's real projection (ULocalPlayer::GetProjectionData, the matrices the renderer uses).
 *
 * The only thing it changes is whether that director's profiler records (UACFCameraDirector::SetProfilerForced), and it
 * gives that back when the target changes, PIE ends or the panel closes. Statistics are computed at UI rate, only for the
 * visible view; nothing is added to the camera evaluation.
 */
class FACFCameraDiagnosticsModel
{
public:
	explicit FACFCameraDiagnosticsModel(FACFCameraTuningPanelModel& InTarget);
	~FACFCameraDiagnosticsModel();
	FACFCameraDiagnosticsModel(const FACFCameraDiagnosticsModel&) = delete;
	FACFCameraDiagnosticsModel& operator=(const FACFCameraDiagnosticsModel&) = delete;

	// --- Profiler (B-1) ------------------------------------------------------------------------------------------
	struct FNodeRow
	{
		FName NodeClass;
		FACFCameraProfileStats Us;
		/** Share of the mean ACF time per frame. */
		double Share = 0.0;
	};

	struct FProfileView
	{
		bool bValid = false;
		/** Recording right now (by this panel and / or by ACF.Profile). */
		bool bRecording = false;
		bool bForcedByPanel = false;
		FString Target;
		/** The statistics period: the frames in the ring buffer (oldest .. newest), and their game time. */
		int32 Capacity = 0;
		uint64 FirstFrame = 0;
		uint64 LastFrame = 0;
		double Seconds = 0.0;
		FACFCameraProfileSummary Summary;
		/** Node classes by mean time, highest first. */
		TArray<FNodeRow> Nodes;
		/** Allocations are measured only while the allocation counter runs (ACF.Profile.Allocations 1). */
		bool bAllocationsCounted = false;
		/** The heaviest section / node of the period. */
		FString Hotspot;
		/** A tuning value changed inside the period: the statistics mix before / after (Reset Statistics to measure again). */
		bool bMixesTuningChange = false;
		uint64 TuningChangeFrame = 0;
	};

	bool StartProfiling();
	void StopProfiling();
	void ResetStatistics();
	bool IsProfiling() const;
	/** Freezes the displayed statistics (recording goes on). */
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }
	/** Recomputes the statistics from the ring buffer (unless paused) and returns them. UI rate only. */
	const FProfileView& UpdateProfile();
	const FProfileView& GetProfile() const { return Profile; }
	/** The tuning panel calls it when a value changes, so a period mixing both is flagged. */
	void NotifyTuningChanged();

	// --- Debugger --------------------------------------------------------------------------------------------------
	struct FRow
	{
		FString Section;
		FString Label;
		FString Value;
	};

	struct FDebugView
	{
		bool bValid = false;
		TArray<FRow> Rows;
		/** Typed copies of what the rows show (tests, comparisons). */
		FACFCameraEvaluationResult Result;
		FACFCameraTransitionInfo Transition;
		FACFCameraHealthDiagnostics Health;
		bool bHasRenderedView = false;
		FVector RenderedLocation = FVector::ZeroVector;
		FRotator RenderedRotation = FRotator::ZeroRotator;
		float RenderedFov = 0.f;
		FString ViewTarget;
		/** ACF's pose is what the player sees (weight 1, not bypassed / suppressed, its view target on screen). */
		bool bAcfOnScreen = false;
		FString OutputState;
	};
	FDebugView ReadDebug() const;
	static const FRow* FindRow(const FDebugView& View, const TCHAR* Label);

	// --- Composition ----------------------------------------------------------------------------------------------
	struct FProjectedPoint
	{
		FString Label;
		FVector World = FVector::ZeroVector;
		/** Pixel in the player's view rectangle (0,0 = its top left) and normalized device coordinates (-1..1, y up). */
		FVector2D Pixel = FVector2D::ZeroVector;
		FVector2D Ndc = FVector2D::ZeroVector;
		bool bInFront = false;
		bool bOnScreen = false;
	};

	struct FCompositionView
	{
		bool bValid = false;
		FString Reason;
		/** The player's view rectangle in the viewport (split screen) and the viewport size. */
		FIntRect ViewRect;
		FIntPoint ViewportSize = FIntPoint::ZeroValue;
		FMatrix ViewProjection = FMatrix::Identity;
		float Fov = 0.f;
		/** Aspect of the player view rectangle, and the aspect the projection really uses (differs when the camera constrains it). */
		float AspectRatio = 0.f;
		float ProjectionAspect = 0.f;
		/** The horizontal FOV the projection really uses (2 atan(1 / M[0][0])); can differ slightly from the PCM FOV. */
		float ProjectionFov = 0.f;
		/** Where the projection is centred (the rendered view origin). */
		FVector ViewOrigin = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		FString ViewTarget;
		bool bAcfOnScreen = false;
		FString OutputState;
		TArray<FProjectedPoint> Points;
	};
	FCompositionView ReadComposition() const;

	/** World point -> pixel in ViewRect / NDC with the view-projection the renderer uses. Returns false if behind the camera. */
	static bool ProjectPoint(const FMatrix& ViewProjection, const FIntRect& ViewRect, const FVector& World, FVector2D& OutPixel, FVector2D& OutNdc);

	/** 2D guides drawn in the PIE viewport of the target player (center, thirds, safe areas, projected targets). */
	void SetGuidesVisible(bool bVisible);
	bool AreGuidesVisible() const { return DrawHandle.IsValid(); }
	/** Draw calls made for the target player (tests: whether the viewport actually drew), and the last canvas size. */
	int32 GetNumGuideDraws() const { return NumGuideDraws; }
	FIntPoint GetLastGuideCanvasSize() const { return LastGuideCanvasSize; }
	/** NDC of the targets as projected by the last guide draw, with the canvas's own SceneView (the rendered view). */
	const TArray<TPair<FString, FVector2D>>& GetLastGuidePoints() const { return LastGuidePoints; }

	// --- Comparison (before / after a tuning change) ----------------------------------------------------------------
	struct FBaseline
	{
		bool bValid = false;
		FString Target;
		uint64 Frame = 0;
		FACFCameraPose AcfPose;
		FVector RenderedLocation = FVector::ZeroVector;
		FRotator RenderedRotation = FRotator::ZeroRotator;
		float RenderedFov = 0.f;
		/** Statistics of the profile period when the baseline was taken (empty if not profiling). */
		FACFCameraProfileStats TotalUs;
		uint64 FirstFrame = 0;
		uint64 LastFrame = 0;
	};
	void CaptureBaseline();
	void ClearBaseline() { Baseline = FBaseline(); }
	const FBaseline& GetBaseline() const { return Baseline; }
	/** "A (baseline) vs now" lines: pose / FOV / time deltas with both sample periods. Empty without a baseline. */
	TArray<FString> DescribeComparison() const;

	/** Drops what refers to a target that changed (profiling of the previous director). Called by the panel's refresh. */
	void Validate();

private:
	UACFCameraDirector* ResolveDirector() const;
	FString DescribeTarget() const;
	void OnPrePIEEnded(bool bIsSimulating);
	void DrawGuides(UCanvas* Canvas, APlayerController* PlayerController);

	FACFCameraTuningPanelModel& Target;
	TWeakObjectPtr<UACFCameraDirector> ProfiledDirector;
	FProfileView Profile;
	bool bPaused = false;
	uint64 TuningChangeFrame = 0;
	FBaseline Baseline;
	FDelegateHandle DrawHandle;
	FDelegateHandle PrePIEEndedHandle;
	int32 NumGuideDraws = 0;
	FIntPoint LastGuideCanvasSize = FIntPoint::ZeroValue;
	TArray<TPair<FString, FVector2D>> LastGuidePoints;
};
