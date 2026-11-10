#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ACFCameraSequence.h"
#include "ACFCameraAnimNotifies.generated.h"

/**
 * Anim notify that steers the finisher / cinematic camera of the montage owner: it sends Command to every camera
 * sequence playback (all local players) whose attacker or target is the mesh owner. ACF never plays or stops the
 * montage; the notify only reports "the animation reached this point".
 *
 * Typical use: Advance Shot on each camera cut of the attack, Finish at the end of the finisher.
 * Does nothing in animation editor previews.
 */
UCLASS(meta = (DisplayName = "ACF Camera Sequence Command"))
class ACTIONCAMERAFRAMEWORK_API UAnimNotify_ACFCameraSequence : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action Camera")
	EACFCameraSequenceCommand Command = EACFCameraSequenceCommand::AdvanceShot;

	/** Jump To Shot only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action Camera", meta = (EditCondition = "Command == EACFCameraSequenceCommand::JumpToShot"))
	FName ShotName;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};

/**
 * Holds the current camera shot (its time does not advance) for the length of the notify state, e.g. while an attack
 * is charged for a variable time. Released at the end of the state, also when the montage is interrupted.
 */
UCLASS(meta = (DisplayName = "ACF Camera Hold Shot"))
class ACTIONCAMERAFRAMEWORK_API UAnimNotifyState_ACFCameraHoldShot : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("ACF Hold Shot"); }
};
