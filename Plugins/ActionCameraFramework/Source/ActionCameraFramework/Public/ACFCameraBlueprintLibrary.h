#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ACFCameraTypes.h"
#include "ACFCameraSequence.h"
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

	/**
	 * Lock-on request params: Player = Follow Target, LockTarget = Look At Target (the Lock-On node's target).
	 * bReleaseWhenTargetLost releases the camera (with its blend-out) when the locked target is destroyed,
	 * and keeps working after Set ACF Camera Lock-On Target switches targets.
	 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Lock-On", meta = (DisplayName = "Make ACF Lock-On Request Params"))
	static FACFCameraRequestParams MakeLockOnRequestParams(int32 Priority, AActor* Player, AActor* LockTarget, bool bReleaseWhenTargetLost = true);

	/** One target group member for an actor (Weight = importance, Radius = sphere kept on screen). */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Target Group", meta = (DisplayName = "Make ACF Camera Group Member"))
	static FACFCameraTargetGroupMember MakeTargetGroupMember(AActor* Actor, float Weight = 1.f, float Radius = 60.f, FVector Offset = FVector::ZeroVector);

	// --- Sequences from anywhere (anim notifies, gameplay abilities): routed by actor -------------------------

	/**
	 * Subsystem of the local player that controls Actor (a pawn, or an actor owned by one), else of the first local
	 * player of Actor's world. Null on dedicated servers.
	 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequence", meta = (DisplayName = "Get ACF Camera Subsystem For Actor"))
	static UACFCameraSubsystem* GetCameraSubsystemForActor(const AActor* Actor);

	/** Starts a finisher on the local player of Attacker (see Get ACF Camera Subsystem For Actor). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Start ACF Finisher Camera For Actor"))
	static FACFCameraSequenceHandle StartFinisherCameraForActor(AActor* Attacker, UACFCameraSequence* Sequence, AActor* Target, float PlayRate = 1.f);

	/**
	 * Sends Command to every playback, on every local player, whose attacker or target is Actor. This is what the ACF
	 * anim notifies call with the mesh owner, so a montage steers "its" finisher camera without knowing the handle.
	 * Returns how many playbacks received it (0 in editor previews and on servers).
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Send ACF Camera Sequence Command For Actor", DefaultToSelf = "Actor"))
	static int32 SendCameraSequenceCommandForActor(AActor* Actor, EACFCameraSequenceCommand Command, FName ShotName = NAME_None);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequence", meta = (DisplayName = "Is Valid (ACF Camera Sequence Handle)", CompactNodeTitle = "IsValid"))
	static bool IsValidSequenceHandle(const FACFCameraSequenceHandle& Handle) { return Handle.IsValid(); }
};
