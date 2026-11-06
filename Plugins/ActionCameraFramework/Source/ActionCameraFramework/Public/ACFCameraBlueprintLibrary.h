#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ACFCameraTypes.h"
#include "ACFCameraBlueprintLibrary.generated.h"

class AActor;
class APlayerController;
class UACFCameraPreset;
class UACFCameraSubsystem;

/** Convenience nodes that resolve the ACF subsystem from a PlayerController. */
UCLASS()
class ACTIONCAMERAFRAMEWORK_API UACFCameraBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Null for remote / non-local player controllers. */
	UFUNCTION(BlueprintPure, Category = "Action Camera", meta = (DisplayName = "Get ACF Camera Subsystem"))
	static UACFCameraSubsystem* GetCameraSubsystem(const APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "Action Camera", meta = (AutoCreateRefTerm = "Params", DisplayName = "Request Camera For Player"))
	static FACFCameraRequestHandle RequestCamera(APlayerController* PlayerController, UACFCameraPreset* Preset, const FACFCameraRequestParams& Params);

	UFUNCTION(BlueprintCallable, Category = "Action Camera", meta = (DisplayName = "Release Camera For Player"))
	static bool ReleaseCamera(APlayerController* PlayerController, UPARAM(ref) FACFCameraRequestHandle& Handle);

	UFUNCTION(BlueprintPure, Category = "Action Camera", meta = (DisplayName = "Is Valid (ACF Camera Request Handle)", CompactNodeTitle = "IsValid"))
	static bool IsValidHandle(const FACFCameraRequestHandle& Handle) { return Handle.IsValid(); }

	/** Builds request params with the given priority and (optional) auto-release instigator. */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	static FACFCameraRequestParams MakeRequestParams(int32 Priority, UObject* Instigator);

	/** Request params with follow / look-at actors (either may be null). */
	UFUNCTION(BlueprintPure, Category = "Action Camera", meta = (DisplayName = "Make Target Request Params"))
	static FACFCameraRequestParams MakeTargetRequestParams(int32 Priority, AActor* FollowTarget, AActor* LookAtTarget, UObject* Instigator);
};
