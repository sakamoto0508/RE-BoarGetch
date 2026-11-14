#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "ACFCameraNode.h"
#include "ACFCameraTypes.h"
#include "Effects/ACFCameraEffectTypes.h"

class UCanvas;
class UWorld;

/**
 * ACF debug visualisation (compiled out of Shipping; costs nothing unless ACF.Debug.Draw > 0).
 *   ACF.Debug.Draw 1 : world (targets, prediction, pivot, collision sweep / hit, camera)
 *   ACF.Debug.Draw 2 : world + screen (dead / soft zones, target on screen, orbit / state text)
 */
namespace ACFCameraDebug
{
	bool IsWorldDrawEnabled();
	bool IsScreenDrawEnabled();
	void DrawWorld(UWorld* World, const FACFCameraRigDebug& Debug, const FACFCameraEvaluationResult& Result, const FACFCameraSolverReport& Solver);
	void DrawCanvas(UCanvas* Canvas, const FACFCameraRigDebug& Debug, const FACFCameraEvaluationResult& Result, const FACFCameraSolverReport& Solver, const TArray<FACFCameraEffectInfo>& Effects, const TArray<FString>& ExtraLines);
}

#endif
