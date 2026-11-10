#include "ACFSequencerBlueprintLibrary.h"
#include "ACFSequencerCameraSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "LevelSequenceActor.h"

UACFSequencerCameraSubsystem* UACFSequencerBlueprintLibrary::GetSequencerCameraSubsystem(const UObject* WorldContextObject)
{
	return UACFSequencerCameraSubsystem::Get(WorldContextObject);
}

FACFSequencerCameraState UACFSequencerBlueprintLibrary::GetSequencerCameraState(const APlayerController* PlayerController)
{
	const UACFSequencerCameraSubsystem* Subsystem = UACFSequencerCameraSubsystem::Get(PlayerController);
	return Subsystem ? Subsystem->GetCameraState(PlayerController) : FACFSequencerCameraState();
}

bool UACFSequencerBlueprintLibrary::IsSequencerControllingCamera(const APlayerController* PlayerController)
{
	const UACFSequencerCameraSubsystem* Subsystem = UACFSequencerCameraSubsystem::Get(PlayerController);
	return Subsystem && Subsystem->IsSequencerControllingCamera(PlayerController);
}

void UACFSequencerBlueprintLibrary::SetSequencerOptionsForPlayer(APlayerController* PlayerController, const FACFSequencerCameraOptions& Options)
{
	if (UACFSequencerCameraSubsystem* Subsystem = UACFSequencerCameraSubsystem::Get(PlayerController))
	{
		Subsystem->SetPlayerOptions(PlayerController, Options);
	}
}

void UACFSequencerBlueprintLibrary::ClearSequencerOptionsForPlayer(APlayerController* PlayerController)
{
	if (UACFSequencerCameraSubsystem* Subsystem = UACFSequencerCameraSubsystem::Get(PlayerController))
	{
		Subsystem->ClearPlayerOptions(PlayerController);
	}
}

void UACFSequencerBlueprintLibrary::SetSequencerOptionsForSequence(ALevelSequenceActor* SequenceActor, const FACFSequencerCameraOptions& Options)
{
	if (UACFSequencerCameraSubsystem* Subsystem = UACFSequencerCameraSubsystem::Get(SequenceActor))
	{
		Subsystem->SetSequenceOptions(SequenceActor, Options);
	}
}

void UACFSequencerBlueprintLibrary::ClearSequencerOptionsForSequence(ALevelSequenceActor* SequenceActor)
{
	if (UACFSequencerCameraSubsystem* Subsystem = UACFSequencerCameraSubsystem::Get(SequenceActor))
	{
		Subsystem->ClearSequenceOptions(SequenceActor);
	}
}
