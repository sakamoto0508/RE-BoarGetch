#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"

/**
 * Phase 6B / B-4: camera output and internal state health (NaN / Inf / invalid FOV / invalid delta time ...).
 *
 * Where invalid values are stopped (one place each, not in every node):
 *   - Target transforms: FACFCameraTarget::Resolve treats a non-finite / non-normalizable transform as "not resolved this
 *     frame" (Hold Last Known keeps the last valid one), so no node stores it in its damping / lookahead state.
 *   - Evaluation context (Director::Evaluate): delta time, UE camera (upstream) pose and control rotation.
 *   - Runtime commands (camera input, orbit rotation, Blend From Pose, blend / suppression times): rejected.
 *   - Node outputs (RigInstance::RunNodes): a node that turns a valid pose invalid is rejected for the invalid
 *     components (the pose before the node is kept) and its interpolation state is reset (location / rotation only).
 *   - Effects, the request blend and the final result (Director): effects are reverted, the final pose falls back to the
 *     last known good pose of the same World / PlayerController, else the UE camera, else ACF writes nothing (weight 0).
 * Every detection is counted and reported (LogActionCamera warning at the 1st, 10th, 100th ... occurrence per issue).
 */
namespace ACFCameraValidation
{
	/** UE 5.8 builds the projection from Max(0.001, FOV) (CameraStackTypes.cpp); 180 degrees and above have no valid projection. */
	constexpr float MinFieldOfView = 0.001f;
	constexpr float MaxFieldOfView = 180.f; // exclusive

	inline bool IsFinite(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
	inline bool IsFinite(const FVector2D& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y); }
	inline bool IsFinite(const FRotator& R) { return FMath::IsFinite(R.Pitch) && FMath::IsFinite(R.Yaw) && FMath::IsFinite(R.Roll); }
	inline bool IsValidFov(float Fov) { return FMath::IsFinite(Fov) && Fov > 0.f && Fov < MaxFieldOfView; }
	inline bool IsValidQuat(const FQuat& Q)
	{
		return FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) && FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W) && Q.SizeSquared() > UE_SMALL_NUMBER;
	}
	inline bool IsValidTransform(const FTransform& T)
	{
		return IsFinite(T.GetLocation()) && IsValidQuat(T.GetRotation()) && IsFinite(T.GetScale3D());
	}
	inline bool IsValidPose(const FACFCameraPose& Pose)
	{
		return IsFinite(Pose.Location) && IsFinite(Pose.Rotation) && IsValidFov(Pose.FieldOfView);
	}
}

/** What was invalid (one counter each). */
enum class EACFCameraHealthIssue : uint8
{
	InvalidTarget,
	InvalidDeltaTime,
	InvalidUpstream,
	InvalidControlRotation,
	RejectedInput,
	InvalidNodeOutput,
	InvalidEffectOutput,
	InvalidBlend,
	InvalidFinalPose,
	InvalidOutputUpstream,
	Num
};

ACTIONCAMERAFRAMEWORK_API const TCHAR* LexToString(EACFCameraHealthIssue Issue);

/**
 * Health counters of one director (= one local player). Reporting never allocates on valid input (only when an issue
 * is reported). While a director evaluates, it is the "active" sink, so code without access to the director
 * (FACFCameraTarget::Resolve, RigInstance) can report into it. Game thread only.
 */
class ACTIONCAMERAFRAMEWORK_API FACFCameraHealth
{
public:
	void Report(EACFCameraHealthIssue Issue, FName Node, const TCHAR* Parameter, const TCHAR* Fallback);
	static void ReportActive(EACFCameraHealthIssue Issue, FName Node, const TCHAR* Parameter, const TCHAR* Fallback);

	void Reset();
	FACFCameraHealthDiagnostics GetDiagnostics() const;
	int32 GetCount(EACFCameraHealthIssue Issue) const { return Counts[static_cast<int32>(Issue)]; }

	/** Owner description for logs (local player / director name). */
	FString OwnerName;
	/** Request whose rig is being evaluated (set by the director). */
	FACFCameraRequestHandle CurrentRequest;
	uint64 CurrentFrame = 0;

	/** Makes Health the active sink for the scope (nested scopes restore the previous one). */
	struct FActiveScope
	{
		explicit FActiveScope(FACFCameraHealth* Health);
		~FActiveScope();
		FACFCameraHealth* Previous;
	};

private:
	int32 Counts[static_cast<int32>(EACFCameraHealthIssue::Num)] = {};
	int32 Total = 0;
	FString LastIssue;
	uint64 LastIssueFrame = 0;
};
