#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionSettings.h"
#include "ACFTransitionTestUtils.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

/**
 * Phase D-1 (README 33 章「音」): the transition's Sound and its SoundStop. Before the fix the sound was fire-and-forget:
 * a looping / long sound went on after Cancel, Interrupt, ClearScreen, a failed load and the end of the transition, and
 * nothing could stop it. Needs an audio device: Scripts/RunTests.sh --with-sound (the normal runs use -nosound and skip).
 * Synchronous (AdvanceForTest). Fades are 0 here (they need the audio thread's time); a fade is checked by ear.
 */
namespace ACFTransitionSound
{
	using namespace ACFTransitionTest;

	struct FSync
	{
		int32 OldCovered = 0;
		bool bOldCapture = true;
		FSync()
		{
			UACFTransitionProjectSettings* P = GetMutableDefault<UACFTransitionProjectSettings>();
			OldCovered = P->CoveredFrames;
			bOldCapture = P->bCaptureScreen;
			P->CoveredFrames = 0;
			P->bCaptureScreen = false;
		}
		~FSync()
		{
			UACFTransitionProjectSettings* P = GetMutableDefault<UACFTransitionProjectSettings>();
			P->CoveredFrames = OldCovered;
			P->bCaptureScreen = bOldCapture;
		}
	};

	/** A looping sound that never ends by itself (silence). */
	static USoundWaveProcedural* MakeLoop()
	{
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage(), NAME_None, RF_Transient);
		Wave->SetSampleRate(48000);
		Wave->NumChannels = 1;
		Wave->Duration = INDEFINITELY_LOOPING_DURATION;
		Wave->SoundGroup = SOUNDGROUP_Default;
		Wave->bLooping = true;
		return Wave;
	}

	/** Audio components of this sound that are playing (also those this subsystem does not track). */
	static int32 Playing(const USoundBase* Sound)
	{
		int32 N = 0;
		for (TObjectIterator<UAudioComponent> It; It; ++It)
		{
			N += (IsValid(*It) && It->Sound == Sound && It->IsPlaying()) ? 1 : 0;
		}
		return N;
	}

	static void StopAll(const USoundBase* Sound)
	{
		for (TObjectIterator<UAudioComponent> It; It; ++It)
		{
			if (IsValid(*It) && It->Sound == Sound)
			{
				It->Stop();
			}
		}
	}

	static void Run(UACFTransitionSubsystem* S, TFunctionRef<bool()> Until, int32 MaxTicks = 400)
	{
		for (int32 I = 0; I < MaxTicks && !Until(); ++I)
		{
			S->AdvanceForTest(0.02f);
		}
	}

	static bool Ended(UACFTransitionSubsystem* S, FACFTransitionHandle H)
	{
		const FACFTransitionInfo Info = S->GetTransitionInfo(H);
		return !Info.bActive && !Info.bQueued && ACFTransition::IsTerminal(Info.LastEvent);
	}

	static FACFTransitionSettings WithSound(EACFTransitionEffect Effect, float Duration, EACFTransitionPhase Phase, USoundBase* Sound, EACFTransitionSoundStop Stop)
	{
		FACFTransitionSettings Out = Settings(Effect, Duration, Phase);
		Out.Sound = Sound;
		Out.SoundStop = Stop;
		Out.SoundFadeOutSeconds = 0.f;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionSoundStopTest, "ActionCameraFramework.Transitions.Sound.StopPolicy", ACFTransitionTest::Flags)
bool FACFTransitionSoundStopTest::RunTest(const FString&)
{
	using namespace ACFTransitionSound;
	UACFTransitionSubsystem* S = FindSubsystem();
	UWorld* World = FindGameWorld();
	if (!S || !World)
	{
		AddWarning(TEXT("Skipped: no game instance (run with -game)."));
		return true;
	}
	if (!World->GetAudioDeviceRaw())
	{
		AddWarning(TEXT("Skipped: no audio device (-nosound); run Scripts/RunTests.sh --with-sound."));
		return true;
	}
	S->ClearScreen();
	S->AdvanceForTest(0.f);
	FSync Sync;
	TStrongObjectPtr<USoundWaveProcedural> Loop(MakeLoop());
	USoundBase* L = Loop.Get();
	FACFTransitionOptions O;
	O.Concurrency = EACFTransitionConcurrency::Interrupt;
	const FACFTransitionSettings OpenPlain = Settings(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Open);

	// PlayToEnd (default, as before): the sound goes on after Cancel.
	FACFTransitionHandle H = S->PlayClose(WithSound(EACFTransitionEffect::BlackFade, 1.f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::PlayToEnd), O);
	S->AdvanceForTest(0.1f);
	TestEqual(TEXT("Sound started with the transition"), Playing(L), 1);
	TestEqual(TEXT("PlayToEnd is not tracked"), S->GetTrackedSoundCount(), 0);
	S->Cancel(H, true);
	TestEqual(TEXT("PlayToEnd: plays on after Cancel"), Playing(L), 1);
	StopAll(L);

	// StopIfAborted: Cancel / Interrupt / ClearScreen / timeout stop it; Finished does not.
	H = S->PlayClose(WithSound(EACFTransitionEffect::BlackFade, 1.f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::StopIfAborted), O);
	S->AdvanceForTest(0.1f);
	TestEqual(TEXT("StopIfAborted: tracked"), S->GetTrackedSoundCount(), 1);
	S->Cancel(H, true);
	TestEqual(TEXT("StopIfAborted: Cancel stops it"), Playing(L), 0);

	H = S->PlayClose(WithSound(EACFTransitionEffect::BlackFade, 1.f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::StopIfAborted), O);
	S->AdvanceForTest(0.1f);
	const FACFTransitionHandle Interrupter = S->PlayOpen(OpenPlain, O);
	TestEqual(TEXT("StopIfAborted: Interrupt stops it"), Playing(L), 0);
	Run(S, [&]() { return Ended(S, Interrupter); });

	H = S->PlayClose(WithSound(EACFTransitionEffect::BlackFade, 1.f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::StopIfAborted), O);
	S->AdvanceForTest(0.1f);
	S->ClearScreen();
	TestEqual(TEXT("StopIfAborted: ClearScreen stops it"), Playing(L), 0);

	H = S->PlayClose(WithSound(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::StopIfAborted), O);
	Run(S, [&]() { return Ended(S, H); });
	TestEqual(TEXT("StopIfAborted: Close finished (Covered): plays on"), Playing(L), 1);
	StopAll(L);
	S->ClearScreen();

	FACFTransitionOptions T = O;
	T.bManualLoad = true;
	T.TimeoutSeconds = 0.3f;
	H = S->PlayLevelTransition(WithSound(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::StopIfAborted), OpenPlain, T, NAME_None, FString());
	Run(S, [&]() { return Ended(S, H); });
	TestEqual(TEXT("StopIfAborted: load timeout (Failed) stops it"), Playing(L), 0);

	// StopWithTransition: ends with the handle, Finished included. A level transition's Close sound plays on while the
	// game loads and stops when the whole transition ends; its Open sound too.
	H = S->PlayClose(WithSound(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::StopWithTransition), O);
	Run(S, [&]() { return Ended(S, H); });
	TestEqual(TEXT("StopWithTransition: stops when the Close finishes"), Playing(L), 0);
	S->ClearScreen();

	FACFTransitionOptions M = O;
	M.bManualLoad = true;
	M.TimeoutSeconds = 60.f;
	H = S->PlayLevelTransition(WithSound(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Close, L, EACFTransitionSoundStop::StopWithTransition),
		WithSound(EACFTransitionEffect::BlackFade, 0.2f, EACFTransitionPhase::Open, L, EACFTransitionSoundStop::StopWithTransition), M, NAME_None, FString());
	Run(S, [&]() { return S->GetCoverage() >= 1.f && S->GetActiveHandle() == H && S->GetTransitionInfo(H).LastEvent == EACFTransitionEvent::Covered; });
	for (int32 I = 0; I < 10; ++I) { S->AdvanceForTest(0.05f); }
	TestEqual(TEXT("Level: the Close sound plays while the game loads"), Playing(L), 1);
	S->NotifyLoadCompleted(H);
	Run(S, [&]() { return S->GetTransitionInfo(H).LastEvent == EACFTransitionEvent::LoadCompleted || Ended(S, H); });
	S->AdvanceForTest(0.02f);
	TestEqual(TEXT("Level: the Open sound joins it"), Playing(L), 2);
	Run(S, [&]() { return Ended(S, H); });
	TestEqual(TEXT("Level: both stop when the transition finishes"), Playing(L), 0);

	TestEqual(TEXT("Nothing tracked at the end"), S->GetTrackedSoundCount(), 0);
	StopAll(L);
	S->ClearScreen();
	S->AdvanceForTest(0.f);
	return true;
}

#endif
