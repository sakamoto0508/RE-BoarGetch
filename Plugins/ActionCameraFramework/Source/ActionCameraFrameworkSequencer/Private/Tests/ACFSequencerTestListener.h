#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFSequencerTypes.h"
#include "ACFSequencerTestListener.generated.h"

class ALevelSequenceActor;
class ULevelSequencePlayer;
class APlayerController;

/**
 * テスト専用: Sequencer 連携のイベント（Dynamic Delegate）を数える。UHT はビルド構成ごとにクラスを除外できないので、
 * テスト以外のビルドにもこの小さなクラスは存在する（クラス選択には出ない）。
 */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class UACFSequencerTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleStarted(APlayerController* PlayerController, ALevelSequenceActor* SequenceActor)
	{
		++NumStarted;
		LastStartedController = PlayerController;
		Record(true, PlayerController, SequenceActor);
		if (OnStarted)
		{
			OnStarted(PlayerController, SequenceActor);
		}
	}

	UFUNCTION()
	void HandleEnded(APlayerController* PlayerController, ALevelSequenceActor* SequenceActor, EACFSequencerReturnKind ReturnKind)
	{
		++NumEnded;
		LastEndedController = PlayerController;
		LastReturnKind = ReturnKind;
		ReturnKinds.Add(ReturnKind);
		Record(false, PlayerController, SequenceActor);
		if (OnEnded)
		{
			OnEnded(PlayerController, SequenceActor);
		}
	}

	/** Phase 6B / B-6: every delivery, in order, with the frame and whether it happened inside a (test PCM) camera update. */
	struct FEvent
	{
		bool bStarted = false;
		uint64 Frame = 0;
		bool bDuringCameraUpdate = false;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<ALevelSequenceActor> Sequence;
	};
	TArray<FEvent> Events;
	/** Blueprint-like reactions run inside the handlers (SetViewTarget, Stop, release a request, ...). */
	TFunction<void(APlayerController*, ALevelSequenceActor*)> OnStarted;
	TFunction<void(APlayerController*, ALevelSequenceActor*)> OnEnded;
	int32 DeliveriesDuringCameraUpdate = 0;

	void Record(bool bStarted, APlayerController* PlayerController, ALevelSequenceActor* SequenceActor);

	/** OnFinished に結び付けて「終了と同じフレームに次の Sequence を再生する」ケースを作る。 */
	UFUNCTION()
	void PlayNext();

	TWeakObjectPtr<ULevelSequencePlayer> Next;
	uint64 NextPlayedFrame = 0;

	int32 NumStarted = 0;
	int32 NumEnded = 0;
	EACFSequencerReturnKind LastReturnKind = EACFSequencerReturnKind::Cut;
	TArray<EACFSequencerReturnKind> ReturnKinds;
	TWeakObjectPtr<APlayerController> LastStartedController;
	TWeakObjectPtr<APlayerController> LastEndedController;
};
