#include "ACFSequencerTestListener.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "GameFramework/PlayerController.h"
#include "ACFSequencerTestCameraManager.h"

void UACFSequencerTestListener::PlayNext()
{
	if (ULevelSequencePlayer* Player = Next.Get())
	{
		NextPlayedFrame = GFrameCounter;
		Player->Play();
	}
}

void UACFSequencerTestListener::Record(bool bStarted, APlayerController* PlayerController, ALevelSequenceActor* SequenceActor)
{
	FEvent& Event = Events.AddDefaulted_GetRef();
	Event.bStarted = bStarted;
	Event.Frame = GFrameCounter;
	Event.bDuringCameraUpdate = AACFSequencerTestCameraManager::IsUpdatingCamera();
	Event.Controller = PlayerController;
	Event.Sequence = SequenceActor;
	DeliveriesDuringCameraUpdate += Event.bDuringCameraUpdate ? 1 : 0;
}
