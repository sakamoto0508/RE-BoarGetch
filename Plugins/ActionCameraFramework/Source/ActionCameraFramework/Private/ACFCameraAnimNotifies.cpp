#include "ACFCameraAnimNotifies.h"
#include "ACFCameraBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"

namespace ACFAnimNotify
{
	static void Send(USkeletalMeshComponent* MeshComp, EACFCameraSequenceCommand Command, FName ShotName)
	{
		if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
		{
			UACFCameraBlueprintLibrary::SendCameraSequenceCommandForActor(Owner, Command, ShotName);
		}
	}
}

void UAnimNotify_ACFCameraSequence::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	ACFAnimNotify::Send(MeshComp, Command, ShotName);
}

FString UAnimNotify_ACFCameraSequence::GetNotifyName_Implementation() const
{
	FString Name = UEnum::GetDisplayValueAsText(Command).ToString();
	if (Command == EACFCameraSequenceCommand::JumpToShot)
	{
		Name += TEXT(" ") + ShotName.ToString();
	}
	return TEXT("ACF ") + Name;
}

void UAnimNotifyState_ACFCameraHoldShot::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	ACFAnimNotify::Send(MeshComp, EACFCameraSequenceCommand::HoldShot, NAME_None);
}

void UAnimNotifyState_ACFCameraHoldShot::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	ACFAnimNotify::Send(MeshComp, EACFCameraSequenceCommand::ReleaseShot, NAME_None);
}
