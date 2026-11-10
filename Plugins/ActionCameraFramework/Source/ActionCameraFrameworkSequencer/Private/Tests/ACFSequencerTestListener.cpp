#include "ACFSequencerTestListener.h"
#include "LevelSequencePlayer.h"

void UACFSequencerTestListener::PlayNext()
{
	if (ULevelSequencePlayer* Player = Next.Get())
	{
		NextPlayedFrame = GFrameCounter;
		Player->Play();
	}
}
