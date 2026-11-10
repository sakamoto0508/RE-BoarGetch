#include "ACFSequencerCameraSubsystem.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraBlueprintLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"

#if !UE_BUILD_SHIPPING

/**
 * 手動確認用（L_ACFCameraTest）。ゲーム側が Level Sequence を再生・停止する操作をコンソールから再現する:
 *   ACF.Sample.Sequencer Play <Cut|Blend|アセットパス> [PlayRate=1]   LS_ACFSample_Cut / LS_ACFSample_Blend、または任意の Level Sequence を再生
 *   ACF.Sample.Sequencer <Stop|Pause|Resume|Status>
 *   ACF.Sample.Sequencer ReturnBlend <Seconds>            カットで戻る場合の ACF の復帰ブレンド（0 = カット）
 *   ACF.Sample.Sequencer ControlRotation <Keep|Match>     戻るときの ControlRotation の扱い
 *   ACF.Sample.Sequencer Reset                            上の 2 つの上書きを消す（Project Settings の既定へ）
 * Shipping ビルドではコンパイルされない。
 */
namespace ACFSequencerDebug
{
	static TWeakObjectPtr<ALevelSequenceActor> GSampleActor;

	static const TCHAR* PhaseName(EACFSequencerCameraPhase Phase)
	{
		switch (Phase)
		{
		case EACFSequencerCameraPhase::BlendingIn: return TEXT("BlendingIn");
		case EACFSequencerCameraPhase::InControl: return TEXT("InControl");
		case EACFSequencerCameraPhase::BlendingOut: return TEXT("BlendingOut");
		default: return TEXT("None");
		}
	}

	static void Usage()
	{
		UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Sample.Sequencer <Play <Cut|Blend> [PlayRate]|Stop|Pause|Resume|Status|ReturnBlend <Seconds>|ControlRotation <Keep|Match>|Reset>"));
	}

	static void SampleSequencer(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		UACFSequencerCameraSubsystem* Sequencer = UACFSequencerCameraSubsystem::Get(World);
		if (!PC || !Sequencer || Args.IsEmpty())
		{
			Usage();
			return;
		}
		const FString& Command = Args[0];
		ALevelSequenceActor* Actor = GSampleActor.Get();
		ULevelSequencePlayer* Player = Actor ? Actor->GetSequencePlayer() : nullptr;

		if (Command == TEXT("Play"))
		{
			const FString Name = Args.IsValidIndex(1) ? Args[1] : TEXT("Cut");
			// 「/」を含む場合は Level Sequence のアセットパスとして扱う（例: /Game/Cinematics/LS_Intro.LS_Intro）。
			const FString Path = Name.Contains(TEXT("/")) ? Name : FString::Printf(TEXT("/ActionCameraFramework/Samples/LS_ACFSample_%s.LS_ACFSample_%s"), *Name, *Name);
			ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *Path);
			if (!Sequence)
			{
				UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Sequencer Play: %s not found (run Scripts/CreateManualTestAssets.py)."), *Path);
				return;
			}
			if (Actor)
			{
				// 連続再生: 前の再生を止めてから作り直す（Stop で ViewTarget は一度ゲームプレイ側へ戻る）。
				if (Player)
				{
					Player->Stop();
				}
				Actor->Destroy();
			}
			FMovieSceneSequencePlaybackSettings Settings;
			Settings.PlayRate = Args.IsValidIndex(2) ? FMath::Max(FCString::Atof(*Args[2]), 0.01f) : 1.f;
			ALevelSequenceActor* NewActor = nullptr;
			ULevelSequencePlayer* NewPlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, NewActor);
			GSampleActor = NewActor;
			if (NewPlayer)
			{
				NewPlayer->Play();
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Sequencer Play %s (rate %.2f)"), *Name, Settings.PlayRate);
		}
		else if (Command == TEXT("Stop") || Command == TEXT("Pause") || Command == TEXT("Resume"))
		{
			if (!Player)
			{
				UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Sequencer %s: nothing is playing."), *Command);
				return;
			}
			if (Command == TEXT("Stop"))
			{
				Player->Stop(); // 途中停止: Camera Cut の Restore でカットで戻る → ACF の復帰ブレンド
			}
			else if (Command == TEXT("Pause"))
			{
				Player->Pause(); // 一時停止中も Sequencer がカメラを握ったまま
			}
			else
			{
				Player->Play();
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Sequencer %s"), *Command);
		}
		else if (Command == TEXT("ReturnBlend") && Args.IsValidIndex(1))
		{
			FACFSequencerCameraOptions Options = Sequencer->GetEffectiveOptions(PC, nullptr);
			Options.ReturnBlend.Duration = FMath::Max(FCString::Atof(*Args[1]), 0.f);
			Options.ReturnBlend.Curve = Options.ReturnBlend.Duration > 0.f ? EACFCameraBlendCurve::EaseInOut : EACFCameraBlendCurve::Cut;
			Sequencer->SetPlayerOptions(PC, Options);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Sequencer ReturnBlend %.2fs"), Options.ReturnBlend.Duration);
		}
		else if (Command == TEXT("ControlRotation") && Args.IsValidIndex(1))
		{
			FACFSequencerCameraOptions Options = Sequencer->GetEffectiveOptions(PC, nullptr);
			Options.ControlRotationPolicy = Args[1] == TEXT("Match") ? EACFSequencerControlRotationPolicy::MatchSequencerCamera : EACFSequencerControlRotationPolicy::Keep;
			Sequencer->SetPlayerOptions(PC, Options);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Sequencer ControlRotation %s"), Options.ControlRotationPolicy == EACFSequencerControlRotationPolicy::Keep ? TEXT("Keep") : TEXT("Match"));
		}
		else if (Command == TEXT("Reset"))
		{
			Sequencer->ClearPlayerOptions(PC);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Sequencer Reset"));
		}
		else if (Command == TEXT("Status"))
		{
			Sequencer->RefreshNow();
			const FACFSequencerCameraState State = Sequencer->GetCameraState(PC);
			const UACFCameraSubsystem* Acf = UACFCameraBlueprintLibrary::GetCameraSubsystem(PC);
			const FACFCameraEvaluationResult Result = Acf ? Acf->GetLastEvaluationResult() : FACFCameraEvaluationResult();
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Sequencer Status: phase=%s sequences=%d viewTarget=%s gameplayTarget=%s | ACF weight=%.2f bypassed=%d suppressed=%d (sources %d) blending=%d requests=%d"),
				PhaseName(State.Phase), State.NumActiveSequences, *GetNameSafe(State.SequencerViewTarget), *GetNameSafe(State.GameplayViewTarget),
				Result.Weight, Result.bOutputBypassed ? 1 : 0, Result.bOutputSuppressed ? 1 : 0, Acf ? Acf->GetNumOutputSuppressions() : 0, Result.bIsBlending ? 1 : 0, Result.NumRequests);
		}
		else
		{
			Usage();
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleSequencerCmd(TEXT("ACF.Sample.Sequencer"),
		TEXT("ACF manual test: play / stop the sample level sequences and tune the return to the gameplay camera."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleSequencer));
}

#endif // !UE_BUILD_SHIPPING
