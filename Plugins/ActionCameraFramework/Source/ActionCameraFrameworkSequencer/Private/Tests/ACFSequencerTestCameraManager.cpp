#include "ACFSequencerTestCameraManager.h"
#include "ACFCameraSubsystem.h"

void AACFSequencerTestCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	if (PassFrame != GFrameCounter)
	{
		PassFrame = GFrameCounter;
		PassesThisFrame = 0;
	}
	++PassesThisFrame;

	// The modifiers only ever see a copy (ACF cannot match the address against ViewTarget.POV / PendingViewTarget.POV).
	AActor* PassViewTarget = &InOutPOV == &PendingViewTarget.POV ? PendingViewTarget.Target.Get() : ViewTarget.Target.Get();
	FMinimalViewInfo Copy = InOutPOV;
	if (Mode == EACFTestPovMode::CopyWithExplicitViewTarget)
	{
		FACFScopedOutputViewTarget Scope(this, PassViewTarget); // the integration API for such camera managers
		Super::ApplyCameraModifiers(DeltaTime, Copy);
	}
	else
	{
		Super::ApplyCameraModifiers(DeltaTime, Copy);
	}
	InOutPOV = Copy;
}

void AACFSequencerTestCameraManager::SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams)
{
	if (Mode == EACFTestPovMode::CopySingleView)
	{
		// Like AGameplayCamerasPlayerCameraManager: never use PendingViewTarget.
		TransitionParams = FViewTargetTransitionParams();
	}
	Super::SetViewTarget(NewViewTarget, TransitionParams);
}
