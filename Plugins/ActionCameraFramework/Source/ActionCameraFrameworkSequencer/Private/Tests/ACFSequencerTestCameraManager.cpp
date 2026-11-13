#include "ACFSequencerTestCameraManager.h"
#include "ACFCameraSubsystem.h"

int32 AACFSequencerTestCameraManager::UpdateDepth = 0;

void AACFSequencerTestCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	if (PassFrame != GFrameCounter)
	{
		PassFrame = GFrameCounter;
		PassesThisFrame = 0;
	}
	++PassesThisFrame;

	if (Mode == EACFTestPovMode::Standard)
	{
		Super::ApplyCameraModifiers(DeltaTime, InOutPOV); // POV by reference, like the base class
		return;
	}

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

void AACFSequencerTestCameraManager::UpdateCamera(float DeltaTime)
{
	++UpdateDepth;
	if (PreUpdate)
	{
		TFunction<void()> Hook = MoveTemp(PreUpdate); // one shot
		PreUpdate.Reset();
		Hook();
	}
	Super::UpdateCamera(DeltaTime);
	--UpdateDepth;
}
