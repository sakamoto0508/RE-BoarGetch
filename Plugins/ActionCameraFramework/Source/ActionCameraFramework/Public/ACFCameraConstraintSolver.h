#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"
#include "Nodes/ACFCameraTargetGroup.h"
#include "ACFCameraConstraintSolver.generated.h"

/**
 * Deoccluder: keeps the player / important targets visible by moving the camera around the pivot.
 * Part of the Collision node's constraint solver (see FACFCameraConstraintSolver).
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraDeoccluderSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder")
	bool bEnabled = false;

	/**
	 * Subjects checked: the Group Framing subjects of the camera (player first, then by weight), else the
	 * player pivot plus the Look At target (lock-on point) when this is on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled"))
	bool bCheckLookAtTarget = true;

	/** Line-of-sight checks per evaluation are bounded by this (player included). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "1", ClampMax = "8"))
	int32 MaxCheckedSubjects = 3;

	/** Radius of the line-of-sight sphere trace (0 = line trace). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = cm))
	float TraceRadius = 6.f;

	/** A subject must stay hidden this long before the camera reacts (passing pillars are ignored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = s))
	float OccludedDelay = 0.25f;

	/** The original position must stay clear this long before the correction is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = s))
	float ClearDelay = 0.5f;

	/** Correction limits: orbit yaw / upward pitch around the pivot, and pulling the camera in (fraction of the arm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "120.0", Units = deg))
	float MaxYawOffset = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "60.0", Units = deg))
	float MaxPitchOffset = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "0.95"))
	float MaxPullIn = 0.6f;

	/** Time constants: moving to a correction / releasing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = s))
	float CorrectionDampingTime = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = s))
	float ReturnDampingTime = 0.7f;

	/** While unresolved, a new search runs at most this often (bounds the trace count). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deoccluder", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = s))
	float RetryInterval = 0.3f;
};

/**
 * Framing recovery: after Collision shortened the camera, widen the FOV (within limits) so the Group Framing
 * subjects stay on screen, and as a last resort rotate a little to keep the player on screen.
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraFramingRecoverySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing Recovery")
	bool bEnabled = false;

	/** Absolute FOV limit of the recovery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing Recovery", meta = (EditCondition = "bEnabled", ClampMin = "5.0", ClampMax = "170.0", Units = deg))
	float MaxFieldOfView = 100.f;

	/** The recovery never adds more than this to the incoming FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing Recovery", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "90.0", Units = deg))
	float MaxFieldOfViewIncrease = 25.f;

	/** If the player still leaves the screen at the FOV limit, rotate towards the player (at most MaxPlayerRotation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing Recovery", meta = (EditCondition = "bEnabled"))
	bool bKeepPlayerOnScreen = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing Recovery", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "60.0", Units = deg))
	float MaxPlayerRotation = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing Recovery", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = s))
	float WidenDampingTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing Recovery", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = s))
	float NarrowDampingTime = 0.6f;
};

/** Per-request runtime state of the deoccluder (fixed size: no per-frame allocation). */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraDeoccluderState
{
	static constexpr int32 MaxSubjects = 8;

	struct FSubjectVisibility
	{
		const void* Key = nullptr; // identity only, never dereferenced
		float BlockedTime = 0.f;
		bool bOccluded = false;
	};
	FSubjectVisibility Subjects[MaxSubjects];
	int32 NumSubjects = 0;

	/** Current (damped) and goal correction: orbit yaw / pitch around the pivot (deg), pull-in fraction. */
	FVector Current = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
	float BaseClearTime = 0.f;
	float TimeSinceSearch = 1000.f;
	int32 Unresolved = 0;
	bool bPlayerUnresolved = false;

	void Reset() { *this = FACFCameraDeoccluderState(); }
};

/** Per-request runtime state of the framing recovery. */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraFramingRecoveryState
{
	float FovDelta = 0.f;
	FVector2D Rotation = FVector2D::ZeroVector; // yaw, pitch added
	bool bInitialized = false;

	void Reset() { *this = FACFCameraFramingRecoveryState(); }
};

/** What the solver did this evaluation (debugging, tests, FACFCameraEvaluationResult). */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSolverReport
{
	int32 NumTraces = 0;
	int32 NumCheckedSubjects = 0;
	/** Subjects confirmed hidden that the correction could not reveal. */
	int32 NumOccluded = 0;
	bool bPlayerOccluded = false;
	bool bSearched = false;
	/** Framing needed more FOV than allowed (enemies may be partly off screen). */
	bool bFramingLimited = false;
	bool bPlayerRotationCorrected = false;
	FVector Correction = FVector::ZeroVector;
	float FovRecovery = 0.f;

#if !UE_BUILD_SHIPPING
	static constexpr int32 MaxDebugLines = 8;
	int32 NumDebugLines = 0;
	FVector DebugFrom[MaxDebugLines];
	FVector DebugTo[MaxDebugLines];
	bool DebugBlocked[MaxDebugLines] = {};
#endif
};

/**
 * World queries used by the solver, injected so the solver is testable without a world.
 *  Collide(Pivot, Desired)  -> camera position after the Collision sweep (instant, no damping)
 *  IsBlocked(From, Subject) -> true if the line of sight from the camera to the subject is blocked
 */
struct FACFCameraSolverQueries
{
	TFunctionRef<FVector(const FVector& Pivot, const FVector& Desired)> Collide;
	TFunctionRef<bool(const FVector& From, const FACFCameraFramingSubject& Subject)> IsBlocked;
};

/**
 * Camera constraint solver, run by the Collision node in the PostBlend stage on the final blended pose:
 *
 *   1. Deocclude   (priority 2 / 3: player, then important targets visible)  -> desired position
 *   2. Collide     (priority 1: the camera never enters geometry; always wins) -> Collision node
 *   3. Recover     (priority 3 / 4: FOV widening within limits, then minimal rotation for the player)
 *
 * Candidates are ordered by how much they move the camera (priority 5) and the search is bounded
 * (at most ~10 candidates, only when an occlusion is confirmed, throttled while unresolved).
 * Unsolvable cases: the player falls back to pulling the camera in front of the occluder (up to MaxPullIn);
 * targets that cannot be revealed stay hidden, are reported (NumOccluded), and the camera does not search
 * every frame. The FOV never exceeds the recovery limits; beyond them targets may leave the frame.
 */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraConstraintSolver
{
	/** Rigid orbit of the camera around Pivot: Correction = (yaw deg, pitch deg upwards, pull-in fraction). */
	static FACFCameraPose ApplyOrbitCorrection(const FACFCameraPose& Pose, const FVector& Pivot, const FVector& Correction);

	static void Deocclude(const FACFCameraDeoccluderSettings& Settings, const FVector& Pivot, const FACFCameraFramingSubjects& Subjects,
		float DeltaTime, FACFCameraDeoccluderState& State, const FACFCameraSolverQueries& Queries, FACFCameraPose& InOutPose, FACFCameraSolverReport& Report);

	static void RecoverFraming(const FACFCameraFramingRecoverySettings& Settings, const FACFCameraFramingSubjects& Subjects, const FVector2D& FramingSize,
		float AspectRatio, float DeltaTime, FACFCameraFramingRecoveryState& State, FACFCameraPose& InOutPose, FACFCameraSolverReport& Report);
};
