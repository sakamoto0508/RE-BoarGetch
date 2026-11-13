#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ACFSequencerTestCameraManager.generated.h"

/** How the test PCM hands the POV to the camera modifiers. */
UENUM()
enum class EACFTestPovMode : uint8
{
	/** Standard APlayerCameraManager flow (ViewTarget / PendingViewTarget passes), but every modifier pass gets a local copy of the POV. */
	CopyPerViewTarget,
	/** Like AGameplayCamerasPlayerCameraManager: no PendingViewTarget (SetViewTarget always cuts), one modifier pass with a local copy. */
	CopySingleView,
	/** CopyPerViewTarget, and the PCM tells ACF which view target each pass belongs to (FACFScopedOutputViewTarget). */
	CopyWithExplicitViewTarget,
	/** Standard flow, POV passed by reference (only the update hooks below are added; B-6). */
	Standard,
};

/**
 * テスト専用（Phase 6B / B-2）: POV をコピーして Camera Modifier に渡す派生 PCM の最小例。
 * 標準 PCM は ViewTarget.POV / PendingViewTarget.POV を参照のまま渡すが、派生 PCM（GameplayCameras など）は
 * ローカル変数にコピーしてから渡すことがある。ACF が POV のアドレスで ViewTarget を判定できない状況を再現する。
 * モードはクラスの既定値で決める（PlayerController が PlayerCameraManagerClass から生成するため）。
 */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class AACFSequencerTestCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	EACFTestPovMode Mode = EACFTestPovMode::CopyPerViewTarget;

	/** Modifier passes of the last updated frame (one per ApplyCameraModifiers call). */
	int32 PassesThisFrame = 0;
	uint64 PassFrame = 0;

	virtual void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
	virtual void SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;
	virtual void UpdateCamera(float DeltaTime) override;

	/** B-6: true while any test PCM runs UpdateCamera (events delivered meanwhile are delivered during a camera update). */
	static bool IsUpdatingCamera() { return UpdateDepth > 0; }
	/** B-6: runs at the start of UpdateCamera, inside the camera update (e.g. "another system stopped the sequence after the ACF tick"). */
	TFunction<void()> PreUpdate;
	static int32 UpdateDepth;
};

UCLASS(NotBlueprintable, HideDropdown, Transient)
class AACFSequencerTestCameraManager_Single : public AACFSequencerTestCameraManager
{
	GENERATED_BODY()

public:
	AACFSequencerTestCameraManager_Single() { Mode = EACFTestPovMode::CopySingleView; }
};

UCLASS(NotBlueprintable, HideDropdown, Transient)
class AACFSequencerTestCameraManager_Explicit : public AACFSequencerTestCameraManager
{
	GENERATED_BODY()

public:
	AACFSequencerTestCameraManager_Explicit() { Mode = EACFTestPovMode::CopyWithExplicitViewTarget; }
};

UCLASS(NotBlueprintable, HideDropdown, Transient)
class AACFSequencerTestCameraManager_Standard : public AACFSequencerTestCameraManager
{
	GENERATED_BODY()

public:
	AACFSequencerTestCameraManager_Standard() { Mode = EACFTestPovMode::Standard; }
};
