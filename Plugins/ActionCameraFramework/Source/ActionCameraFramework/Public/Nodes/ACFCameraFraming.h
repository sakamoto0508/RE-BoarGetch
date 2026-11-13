#pragma once

#include "CoreMinimal.h"
#include "ACFCameraFraming.generated.h"

/**
 * Where a target should appear on screen. Screen coordinates are normalised and resolution /
 * FOV / aspect independent: (0, 0) is the centre, X = +0.5 the right edge, Y = +0.5 the top edge.
 * Zone sizes are fractions of the screen (1 = full width / height).
 *
 *  - inside the dead zone : no correction
 *  - in the soft zone     : corrected towards the dead zone with damping
 *  - outside the soft zone: never allowed (hard correction this frame)
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraScreenComposition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (UIMin = "-0.5", UIMax = "0.5"))
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	FVector2D DeadZoneSize = FVector2D(0.1f, 0.1f);

	/** Must be at least the dead zone (it is widened automatically otherwise). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	FVector2D SoftZoneSize = FVector2D(0.8f, 0.8f);
};

/** Pinhole projection helpers matching UE's horizontal-FOV perspective (MaintainXFOV). */
namespace ACFFraming
{
	struct FLens
	{
		double TanHalfH = 1.0;
		double TanHalfV = 1.0;
	};

	/**
	 * Lens of a pose FOV. HorizontalFovScale (FACFCameraEvaluationContext) maps the pose FOV to the rendered one: the half
	 * horizontal tangent is tan(FOV / 2) * HorizontalFovScale, the vertical one that divided by the view aspect.
	 */
	ACTIONCAMERAFRAMEWORK_API FLens MakeLens(float HorizontalFovDegrees, float AspectRatio, float HorizontalFovScale = 1.f);

	/**
	 * Phase 7A: the lens the renderer really uses for a view, from the engine's own projection code
	 * (FMinimalViewInfo::CalculateProjectionMatrixGivenView / ...GivenViewRectangle): OutHorizontalFovScale = rendered half
	 * horizontal tangent / tan(pose FOV / 2) (exact for every FOV: all modes scale the tangent linearly) and OutAspectRatio =
	 * the aspect it projects with. InOutViewSettings carries the camera's projection settings (aspect, constraint, axis
	 * constraint override, crop, off-centre, near plane); its FOV is overwritten with the 90 degree reference. Viewport is only
	 * needed for a constrained aspect (letterbox); without it the rectangle is used as it is. False for a degenerate or
	 * orthographic view (keep the classic lens).
	 */
	ACTIONCAMERAFRAMEWORK_API bool ComputeRenderedLens(struct FMinimalViewInfo& InOutViewSettings, int32 AspectRatioAxisConstraint, const FIntRect& ViewRect,
		class FViewport* Viewport, float& OutHorizontalFovScale, float& OutAspectRatio);

	/** Projects WorldPoint. False if it is behind (or on) the camera plane. */
	ACTIONCAMERAFRAMEWORK_API bool Project(const FVector& CameraLocation, const FRotator& CameraRotation, const FLens& Lens, const FVector& WorldPoint, FVector2D& OutScreen, double& OutDepth);

	/** New screen coordinate on one axis after dead / soft zone handling (Alpha = damping step 0..1). */
	ACTIONCAMERAFRAMEWORK_API double ComposeAxis(double Screen, double Desired, double DeadSize, double SoftSize, float Alpha);

	/** Rotation (roll 0) placing WorldPoint at DesiredScreen, starting from Rotation. Iterative, converges in a few steps. */
	ACTIONCAMERAFRAMEWORK_API FRotator AimAt(const FRotator& Rotation, const FVector& CameraLocation, const FLens& Lens, const FVector& WorldPoint, const FVector2D& DesiredScreen);
}
