#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "ACFCameraSequence.h"
#include "ACFCameraSequencePlayer.h"
#include "Effects/ACFCameraEffect.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 4C: camera sequences / finisher cameras on the director alone (no world). Fixed-pose shots make every frame's
 * expected pose exact; dt = 0.125 keeps sequence time exact in float.
 */
namespace ACFSequenceTests
{
	using namespace ACFTests;

	constexpr float Dt = 0.125f;
	static const FVector GameplayLoc(0.f, 0.f, 100.f);
	static const FVector LocA(1000.f, 0.f, 0.f);
	static const FVector LocB(0.f, 1000.f, 0.f);
	static const FVector LocC(0.f, 0.f, 1000.f);
	static const FVector LocX(-500.f, -500.f, 0.f);

	struct FRig
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		FFrameDriver Driver;
		TArray<FACFCameraSequenceEvent> Events;
		TArray<TStrongObjectPtr<UObject>> Keep;

		FRig() : Director(NewObject<UACFCameraDirector>()) {}

		const FACFCameraEvaluationResult& Step(float DeltaTime = Dt)
		{
			const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), DeltaTime);
			Collect();
			return Result;
		}

		void Collect()
		{
			TArray<FACFCameraSequenceEvent> New;
			Director->ConsumeSequenceEvents(New);
			Events.Append(New);
		}

		FACFCameraSequenceState State(FACFCameraSequenceHandle Handle) const
		{
			FACFCameraSequenceState Out;
			Director->GetSequenceState(Handle, Out);
			return Out;
		}

		int32 Count(EACFCameraSequenceEventType Type, EACFCameraSequenceEndReason Reason = EACFCameraSequenceEndReason::None, int32 Shot = -2) const
		{
			int32 N = 0;
			for (const FACFCameraSequenceEvent& E : Events)
			{
				if (E.Type == Type && (Reason == EACFCameraSequenceEndReason::None || E.Reason == Reason) && (Shot == -2 || E.ShotIndex == Shot))
				{
					++N;
				}
			}
			return N;
		}

		UACFCameraPreset* Fixed(const FVector& Location, float FOV = 70.f, const FACFCameraBlendSettings& Blend = FACFCameraBlendSettings::MakeCut())
		{
			UACFCameraPreset* Preset = MakeFixedPreset(Location, FOV, Blend);
			Keep.Emplace(Preset);
			return Preset;
		}

		FACFCameraRequestHandle Gameplay(int32 Priority = 0)
		{
			FACFCameraRequestParams Params;
			Params.Priority = Priority;
			Params.DebugName = TEXT("Gameplay");
			return Director->AddRequest(Fixed(GameplayLoc), Params);
		}

		UACFCameraSequence* NewSequence()
		{
			UACFCameraSequence* Sequence = NewObject<UACFCameraSequence>(GetTransientPackage(), NAME_None, RF_Transient);
			Keep.Emplace(Sequence);
			return Sequence;
		}

		FACFCameraShot& AddShot(UACFCameraSequence* Sequence, FName Name, const FVector& Location, float Duration,
			const FACFCameraBlendSettings& BlendIn = FACFCameraBlendSettings::MakeCut(),
			EACFCameraShotAdvanceMode Mode = EACFCameraShotAdvanceMode::Duration)
		{
			FACFCameraShot& Shot = Sequence->Shots.AddDefaulted_GetRef();
			Shot.Name = Name;
			Shot.Preset = Fixed(Location);
			Shot.Duration = Duration;
			Shot.BlendIn = BlendIn;
			Shot.AdvanceMode = Mode;
			return Shot;
		}

		UACFCameraEffectPreset* Effect(float Sustain, float Release)
		{
			UACFCameraEffectPreset* Preset = NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient);
			UACFCameraEffect_Punch* Layer = NewObject<UACFCameraEffect_Punch>(Preset);
			Layer->Location = FVector::ZeroVector;
			Layer->FOV = 1.f;
			Layer->Envelope.Attack = 0.f;
			Layer->Envelope.Sustain = Sustain;
			Layer->Envelope.Release = Release;
			Layer->Envelope.Shape = EACFCameraEnvelopeShape::Linear;
			Preset->Layers.Add(Layer);
			Keep.Emplace(Preset);
			return Preset;
		}
	};

	static bool PoseAt(FAutomationTestBase& Test, const FACFCameraEvaluationResult& Result, const FVector& Expected, const TCHAR* What)
	{
		const bool bOk = Result.Pose.Location.Equals(Expected, 0.01);
		Test.TestTrue(FString::Printf(TEXT("%s: pose %s == %s"), What, *Result.Pose.Location.ToString(), *Expected.ToString()), bOk);
		return bOk;
	}
}

using namespace ACFSequenceTests;

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceFinisherStartEndTest, "ActionCameraFramework.Sequence.FinisherStartAndEnd", ACFTests::Flags)
bool FACFSequenceFinisherStartEndTest::RunTest(const FString& Parameters)
{
	FRig R;
	const FACFCameraRequestHandle Gameplay = R.Gameplay();
	R.Step();
	PoseAt(*this, R.Step(), GameplayLoc, TEXT("gameplay camera before"));

	const FACFCameraSequenceHandle Handle = R.Director->PlayFinisherPreset(R.Fixed(LocA), nullptr, nullptr, 1.f,
		FACFCameraBlendSettings::MakeCut(), Linear(0.5f), 1000);
	TestTrue(TEXT("handle valid"), Handle.IsValid());
	TestEqual(TEXT("first shot starts on the next camera update"), R.State(Handle).ShotIndex, INDEX_NONE);

	const FACFCameraEvaluationResult& First = R.Step();
	PoseAt(*this, First, LocA, TEXT("finisher shot cut in the same frame"));
	FACFCameraSequenceState State = R.State(Handle);
	TestEqual(TEXT("shot 0"), State.ShotIndex, 0);
	TestTrue(TEXT("finisher in control"), State.bInControl);
	TestTrue(TEXT("finisher request priority"), First.ActiveRequest == State.ShotRequest);
	TestEqual(TEXT("gameplay + finisher requests"), R.Director->GetNumRequests(), 2);
	TestEqual(TEXT("ShotStarted"), R.Count(EACFCameraSequenceEventType::ShotStarted), 1);

	for (int32 I = 0; I < 6; ++I)
	{
		R.Step();
	}
	TestTrue(TEXT("still playing at 0.875s"), R.Director->IsSequencePlaying(Handle));
	const FACFCameraEvaluationResult& End = R.Step(); // 1.0s
	TestFalse(TEXT("ended after Duration"), R.Director->IsSequencePlaying(Handle));
	TestEqual(TEXT("only the gameplay request remains"), R.Director->GetNumRequests(), 1);
	TestTrue(TEXT("back to the previous camera"), End.ActiveRequest == Gameplay);
	TestTrue(TEXT("return blend running"), End.bIsBlending);
	TestEqual(TEXT("return blend alpha after one frame of 0.5s linear"), End.BlendAlpha, 0.25f, 1e-4f);
	PoseAt(*this, End, FMath::Lerp(LocA, GameplayLoc, 0.25), TEXT("return blend from the finisher shot"));
	TestEqual(TEXT("ShotEnded Completed"), R.Count(EACFCameraSequenceEventType::ShotEnded, EACFCameraSequenceEndReason::Completed), 1);
	TestEqual(TEXT("SequenceEnded Completed"), R.Count(EACFCameraSequenceEventType::SequenceEnded, EACFCameraSequenceEndReason::Completed), 1);
	for (int32 I = 0; I < 4; ++I)
	{
		R.Step();
	}
	PoseAt(*this, R.Step(), GameplayLoc, TEXT("returned"));

	// Finisher with Duration 0 waits for Finish (the gameplay event).
	const FACFCameraSequenceHandle Open = R.Director->PlayFinisherPreset(R.Fixed(LocB), nullptr, nullptr, 0.f, FACFCameraBlendSettings::MakeCut(), FACFCameraBlendSettings::MakeCut(), 1000);
	for (int32 I = 0; I < 40; ++I)
	{
		R.Step();
	}
	TestTrue(TEXT("Duration 0 finisher waits for the event (5s later)"), R.Director->IsSequencePlaying(Open));
	TestTrue(TEXT("Finish"), R.Director->SendSequenceCommand(Open, EACFCameraSequenceCommand::Finish));
	R.Collect();
	TestEqual(TEXT("SequenceEnded by Finish = Advanced"), R.Count(EACFCameraSequenceEventType::SequenceEnded, EACFCameraSequenceEndReason::Advanced), 1);
	PoseAt(*this, R.Step(), GameplayLoc, TEXT("cut back after Finish"));
	TestFalse(TEXT("stale handle rejected"), R.Director->SendSequenceCommand(Open, EACFCameraSequenceCommand::Cancel));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceShotsCutBlendTest, "ActionCameraFramework.Sequence.ShotsCutAndBlend", ACFTests::Flags)
bool FACFSequenceShotsCutBlendTest::RunTest(const FString& Parameters)
{
	FRig R;
	const FACFCameraRequestHandle Gameplay = R.Gameplay();
	R.Step();
	UACFCameraSequence* Sequence = R.NewSequence();
	R.AddShot(Sequence, TEXT("Wide"), LocA, 0.5f);
	R.AddShot(Sequence, TEXT("Side"), LocB, 0.5f, Linear(0.5f));
	R.AddShot(Sequence, TEXT("Close"), LocC, 0.5f);
	Sequence->ReturnBlend = FACFCameraBlendSettings::MakeCut();
	const FACFCameraSequenceHandle Handle = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());

	PoseAt(*this, R.Step(), LocA, TEXT("shot 0 cut"));
	R.Step();
	R.Step();
	const FACFCameraEvaluationResult& BlendStart = R.Step(); // 0.5s: shot 1 starts, blends 0.5s from shot 0
	TestEqual(TEXT("shot 1"), R.State(Handle).ShotIndex, 1);
	TestTrue(TEXT("blend into shot 1"), BlendStart.bIsBlending);
	PoseAt(*this, BlendStart, FMath::Lerp(LocA, LocB, 0.25), TEXT("shot 1 blend 25%"));
	TestEqual(TEXT("old shot request released (gameplay + shot 1)"), R.Director->GetNumRequests(), 2);
	PoseAt(*this, R.Step(), FMath::Lerp(LocA, LocB, 0.5), TEXT("shot 1 blend 50%"));
	R.Step();
	PoseAt(*this, R.Step(), LocB, TEXT("shot 1 reached"));
	PoseAt(*this, R.Step(), LocC, TEXT("shot 2 cut (no blend)"));
	TestEqual(TEXT("shot 2"), R.State(Handle).ShotIndex, 2);
	TestEqual(TEXT("still exactly one shot request"), R.Director->GetNumRequests(), 2);
	R.Step();
	R.Step();
	R.Step();
	const FACFCameraEvaluationResult& Back = R.Step();
	TestFalse(TEXT("sequence complete"), R.Director->IsSequencePlaying(Handle));
	PoseAt(*this, Back, GameplayLoc, TEXT("cut back to gameplay"));
	TestTrue(TEXT("gameplay active"), Back.ActiveRequest == Gameplay);

	// Notification order.
	const EACFCameraSequenceEventType Started = EACFCameraSequenceEventType::ShotStarted;
	const EACFCameraSequenceEventType Ended = EACFCameraSequenceEventType::ShotEnded;
	const TArray<TPair<EACFCameraSequenceEventType, int32>> Expected = {
		{ Started, 0 }, { Ended, 0 }, { Started, 1 }, { Ended, 1 }, { Started, 2 }, { Ended, 2 }, { EACFCameraSequenceEventType::SequenceEnded, 2 } };
	TestEqual(TEXT("event count"), R.Events.Num(), Expected.Num());
	for (int32 I = 0; I < FMath::Min(Expected.Num(), R.Events.Num()); ++I)
	{
		TestTrue(FString::Printf(TEXT("event %d"), I), R.Events[I].Type == Expected[I].Key && R.Events[I].ShotIndex == Expected[I].Value);
	}
	TestEqual(TEXT("ShotStarted carries the shot name"), R.Events[2].ShotName, FName(TEXT("Side")));

	// A long frame (hitch) covering whole shots: they are reported, not shown, and the overflow carries over.
	const FACFCameraSequenceHandle Second = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	R.Step(0.f); // starts shot 0 (zero delta)
	R.Events.Reset();
	PoseAt(*this, R.Step(1.125f), LocC, TEXT("hitch lands on shot 2"));
	const FACFCameraSequenceState State = R.State(Second);
	TestEqual(TEXT("hitch: shot 2"), State.ShotIndex, 2);
	TestEqual(TEXT("hitch: carry 0.125"), State.ShotTime, 0.125f, 1e-4f);
	TestEqual(TEXT("hitch: shots 0..1 ended Completed"), R.Count(Ended, EACFCameraSequenceEndReason::Completed), 2);
	TestEqual(TEXT("hitch: one shot request"), R.Director->GetNumRequests(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceEventsTest, "ActionCameraFramework.Sequence.EventsSkipHold", ACFTests::Flags)
bool FACFSequenceEventsTest::RunTest(const FString& Parameters)
{
	FRig R;
	R.Gameplay();
	UACFCameraSequence* Sequence = R.NewSequence();
	R.AddShot(Sequence, TEXT("Charge"), LocA, 0.f, FACFCameraBlendSettings::MakeCut(), EACFCameraShotAdvanceMode::WaitForEvent);
	R.AddShot(Sequence, TEXT("Strike"), LocB, 1.f);
	R.AddShot(Sequence, TEXT("Extra"), LocX, 1.f);
	R.AddShot(Sequence, TEXT("Finale"), LocC, 0.5f, FACFCameraBlendSettings::MakeCut(), EACFCameraShotAdvanceMode::WaitForEvent);
	const FACFCameraSequenceHandle Handle = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());

	for (int32 I = 0; I < 24; ++I)
	{
		R.Step();
	}
	FACFCameraSequenceState State = R.State(Handle);
	TestEqual(TEXT("event shot without timeout waits (3s)"), State.ShotIndex, 0);
	TestTrue(TEXT("waiting for event"), State.bWaitingForEvent);

	// Two Advance events in one frame advance once.
	TestTrue(TEXT("advance 1"), R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::AdvanceShot));
	TestTrue(TEXT("advance 2 (same frame)"), R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::AdvanceShot));
	TestEqual(TEXT("latched until the camera update"), R.State(Handle).ShotIndex, 0);
	PoseAt(*this, R.Step(), LocB, TEXT("advanced once"));
	TestEqual(TEXT("shot 1"), R.State(Handle).ShotIndex, 1);
	TestEqual(TEXT("shot 0 ended by event"), R.Count(EACFCameraSequenceEventType::ShotEnded, EACFCameraSequenceEndReason::Advanced, 0), 1);

	// Hold (counted) freezes shot time.
	R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::HoldShot);
	R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::HoldShot);
	const float Before = R.State(Handle).ShotTime;
	for (int32 I = 0; I < 16; ++I)
	{
		R.Step();
	}
	TestEqual(TEXT("held: time frozen"), R.State(Handle).ShotTime, Before);
	TestEqual(TEXT("held: still shot 1 after 2s (Duration 1s)"), R.State(Handle).ShotIndex, 1);
	R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::ReleaseShot);
	R.Step();
	TestTrue(TEXT("one Release of two Holds keeps it held"), R.State(Handle).bHeld);
	R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::ReleaseShot);
	R.Step();
	TestEqual(TEXT("released: time runs"), R.State(Handle).ShotTime, Before + Dt, 1e-4f);

	// Play rate 2: the rest of the shot (1 - 0.25) takes 0.375s.
	R.Director->SetSequencePlayRate(Handle, 2.f);
	R.Step();
	R.Step();
	TestEqual(TEXT("rate 2: still shot 1"), R.State(Handle).ShotIndex, 1);
	R.Step();
	TestEqual(TEXT("rate 2: shot 2 after 0.375s"), R.State(Handle).ShotIndex, 2);
	R.Director->SetSequencePlayRate(Handle, 1.f);

	// Jump (skip) by name; the last Jump of a frame wins over Advance.
	TestFalse(TEXT("unknown shot name rejected"), R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::JumpToShot, TEXT("Nope")));
	R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::AdvanceShot);
	R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::JumpToShot, TEXT("Finale"));
	PoseAt(*this, R.Step(), LocC, TEXT("jumped to Finale"));
	TestEqual(TEXT("shot 3"), R.State(Handle).ShotIndex, 3);
	TestEqual(TEXT("shot 2 ended Skipped"), R.Count(EACFCameraSequenceEventType::ShotEnded, EACFCameraSequenceEndReason::Skipped, 2), 1);

	// Wait-for-event shot with a timeout ends on its own.
	R.Step();
	R.Step();
	R.Step();
	R.Step();
	TestFalse(TEXT("timeout 0.5s ended the last shot"), R.Director->IsSequencePlaying(Handle));
	TestEqual(TEXT("Completed"), R.Count(EACFCameraSequenceEventType::SequenceEnded, EACFCameraSequenceEndReason::Completed), 1);

	// Same-frame guard: a repeated evaluation in the same frame advances nothing.
	const FACFCameraSequenceHandle Again = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	R.Director->SendSequenceCommand(Again, EACFCameraSequenceCommand::AdvanceShot);
	R.Step();
	const float T = R.State(Again).ShotTime;
	R.Director->SendSequenceCommand(Again, EACFCameraSequenceCommand::AdvanceShot);
	R.Driver.Repeat(R.Director.Get(), Dt);
	R.Driver.Repeat(R.Director.Get(), Dt);
	TestEqual(TEXT("repeat evaluation: no time"), R.State(Again).ShotTime, T);
	TestEqual(TEXT("repeat evaluation: Advance not applied twice / not yet"), R.State(Again).ShotIndex, 1);
	R.Step();
	TestEqual(TEXT("next frame applies the latched Advance once"), R.State(Again).ShotIndex, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceCancelEffectsTest, "ActionCameraFramework.Sequence.CancelAndEffectOwnership", ACFTests::Flags)
bool FACFSequenceCancelEffectsTest::RunTest(const FString& Parameters)
{
	FRig R;
	const FACFCameraRequestHandle Gameplay = R.Gameplay();
	R.Step();
	UACFCameraSequence* Sequence = R.NewSequence();
	FACFCameraShot& S0 = R.AddShot(Sequence, TEXT("A"), LocA, 0.5f);
	S0.Effects.Add({ R.Effect(-1.f, 0.25f), 1.f, EACFCameraShotEffectLifetime::Shot });
	S0.Effects.Add({ R.Effect(-1.f, 0.25f), 1.f, EACFCameraShotEffectLifetime::Sequence });
	S0.Effects.Add({ R.Effect(0.f, 2.f), 1.f, EACFCameraShotEffectLifetime::FireAndForget });
	R.AddShot(Sequence, TEXT("B"), LocB, 0.5f);
	Sequence->ReturnBlend = FACFCameraBlendSettings::MakeCut();

	// Natural end: shot effects released at the shot end, sequence effects at the sequence end; none remains.
	FACFCameraSequenceHandle Handle = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	R.Step();
	TestEqual(TEXT("3 effects playing"), R.Director->GetNumActiveEffects(), 3);
	TestEqual(TEXT("3 owned"), R.State(Handle).NumOwnedEffects, 3);
	for (int32 I = 0; I < 3; ++I)
	{
		R.Step();
	}
	TestEqual(TEXT("shot 1"), R.State(Handle).ShotIndex, 1);
	TestEqual(TEXT("shot effect released (sequence + one-shot owned)"), R.State(Handle).NumOwnedEffects, 2);
	for (int32 I = 0; I < 4; ++I)
	{
		R.Step();
	}
	TestFalse(TEXT("sequence complete"), R.Director->IsSequencePlaying(Handle));
	for (int32 I = 0; I < 3; ++I)
	{
		R.Step();
	}
	TestEqual(TEXT("infinite effects gone after their release; only the one-shot finishes on its own"), R.Director->GetNumActiveEffects(), 1);
	for (int32 I = 0; I < 12; ++I)
	{
		R.Step();
	}
	TestEqual(TEXT("no effect left"), R.Director->GetNumActiveEffects(), 0);

	// Cancel mid-shot: request gone at once, every owned effect cut, cut back to gameplay.
	Handle = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	R.Step();
	R.Events.Reset();
	TestEqual(TEXT("cancel: 3 effects playing"), R.Director->GetNumActiveEffects(), 3);
	TestTrue(TEXT("cancel"), R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::Cancel));
	R.Collect();
	TestEqual(TEXT("cancel: request removed immediately"), R.Director->GetNumRequests(), 1);
	TestEqual(TEXT("cancel: all effects cut (also the one-shot)"), R.Director->GetNumActiveEffects(), 0);
	TestEqual(TEXT("cancel: ShotEnded Cancelled"), R.Count(EACFCameraSequenceEventType::ShotEnded, EACFCameraSequenceEndReason::Cancelled, 0), 1);
	TestEqual(TEXT("cancel: SequenceEnded Cancelled"), R.Count(EACFCameraSequenceEventType::SequenceEnded, EACFCameraSequenceEndReason::Cancelled), 1);
	const FACFCameraEvaluationResult& Back = R.Step();
	PoseAt(*this, Back, GameplayLoc, TEXT("cancel blend = cut"));
	TestTrue(TEXT("gameplay in control"), Back.ActiveRequest == Gameplay);

	// Replaced by a new playback (Stop Other Sequences): the old one's request and effects are cleaned up.
	const FACFCameraSequenceHandle First = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	R.Step();
	const FACFCameraSequenceHandle Second = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	R.Collect();
	TestFalse(TEXT("first replaced"), R.Director->IsSequencePlaying(First));
	TestEqual(TEXT("Replaced reported"), R.Count(EACFCameraSequenceEventType::SequenceEnded, EACFCameraSequenceEndReason::Replaced), 1);
	R.Step();
	TestEqual(TEXT("one sequence, one shot request"), R.Director->GetNumRequests(), 2);
	TestEqual(TEXT("one playback"), R.Director->GetNumSequences(), 1);
	R.Director->SendSequenceCommand(Second, EACFCameraSequenceCommand::Cancel);
	for (int32 I = 0; I < 20; ++I)
	{
		R.Step();
	}
	TestEqual(TEXT("replaced + cancelled: no effect left once the released ones finished"), R.Director->GetNumActiveEffects(), 0);

	// Somebody else releases the shot request (Release All Cameras): the playback ends RequestLost and frees effects.
	const FACFCameraSequenceHandle Lost = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	R.Step();
	R.Director->RemoveAllRequests(false);
	R.Step();
	TestFalse(TEXT("request lost ends the playback"), R.Director->IsSequencePlaying(Lost));
	TestEqual(TEXT("RequestLost reported"), R.Count(EACFCameraSequenceEventType::SequenceEnded, EACFCameraSequenceEndReason::RequestLost), 1);
	for (int32 I = 0; I < 20; ++I)
	{
		R.Step();
	}
	TestEqual(TEXT("request lost: effects stopped"), R.Director->GetNumActiveEffects(), 0);

	// Instigator destroyed.
	UObject* Owner = NewObject<UACFCameraPreset>(GetTransientPackage());
	FACFCameraSequenceParams Params;
	Params.Instigator = Owner;
	R.Gameplay();
	const FACFCameraSequenceHandle Owned = R.Director->PlaySequence(Sequence, Params);
	R.Step();
	Owner->MarkAsGarbage();
	R.Step();
	TestFalse(TEXT("instigator lost ends the playback"), R.Director->IsSequencePlaying(Owned));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceInterruptTest, "ActionCameraFramework.Sequence.InterruptAndReturn", ACFTests::Flags)
bool FACFSequenceInterruptTest::RunTest(const FString& Parameters)
{
	for (const EACFCameraSequenceInterruptPolicy Policy : { EACFCameraSequenceInterruptPolicy::Continue, EACFCameraSequenceInterruptPolicy::Pause, EACFCameraSequenceInterruptPolicy::Cancel })
	{
		const FString Name = UEnum::GetValueAsString(Policy);
		FRig R;
		R.Gameplay();
		UACFCameraSequence* Sequence = R.NewSequence();
		R.AddShot(Sequence, TEXT("A"), LocA, 2.f);
		Sequence->InterruptPolicy = Policy;
		const FACFCameraSequenceHandle Handle = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
		R.Step();
		R.Step();

		FACFCameraRequestParams High;
		High.Priority = 5000;
		const FACFCameraRequestHandle Interrupt = R.Director->AddRequest(R.Fixed(LocX), High);
		for (int32 I = 0; I < 4; ++I)
		{
			PoseAt(*this, R.Step(), LocX, *(Name + TEXT(": interrupt in control")));
		}
		if (Policy == EACFCameraSequenceInterruptPolicy::Cancel)
		{
			TestFalse(*(Name + TEXT(": cancelled")), R.Director->IsSequencePlaying(Handle));
			TestEqual(*(Name + TEXT(": reason Interrupted")), R.Count(EACFCameraSequenceEventType::SequenceEnded, EACFCameraSequenceEndReason::Interrupted), 1);
			TestEqual(*(Name + TEXT(": no shot request left")), R.Director->GetNumRequests(), 2);
			continue;
		}
		const FACFCameraSequenceState State = R.State(Handle);
		TestFalse(*(Name + TEXT(": not in control")), State.bInControl);
		const float Expected = Policy == EACFCameraSequenceInterruptPolicy::Pause ? 2.f * Dt : 6.f * Dt;
		TestEqual(*(Name + TEXT(": shot time")), State.ShotTime, Expected, 1e-4f);
		R.Director->RemoveRequest(Interrupt);
		PoseAt(*this, R.Step(), LocA, *(Name + TEXT(": shot shows again after the interruption")));
		TestTrue(*(Name + TEXT(": in control again")), R.State(Handle).bInControl);
	}

	// Lock-on (lower request) -> finisher -> back: the sequence's Return Blend is used, not the lock-on's cut blend-in.
	{
		FRig R;
		const FACFCameraRequestHandle LockOn = R.Gameplay(10);
		R.Step();
		UACFCameraSequence* Sequence = R.NewSequence();
		R.AddShot(Sequence, TEXT("A"), LocA, 0.f, FACFCameraBlendSettings::MakeCut(), EACFCameraShotAdvanceMode::WaitForEvent);
		Sequence->ReturnBlend = Linear(0.5f);
		const FACFCameraSequenceHandle Handle = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
		PoseAt(*this, R.Step(), LocA, TEXT("lock-on -> finisher"));
		TestTrue(TEXT("lock-on request still alive below the finisher"), R.Director->HasRequest(LockOn));
		R.Director->SendSequenceCommand(Handle, EACFCameraSequenceCommand::Finish);
		const FACFCameraEvaluationResult& Back = R.Step();
		TestTrue(TEXT("lock-on in control again"), Back.ActiveRequest == LockOn);
		TestTrue(TEXT("return blend (not the lock-on's cut)"), Back.bIsBlending);
		PoseAt(*this, Back, FMath::Lerp(LocA, GameplayLoc, 0.25), TEXT("return blend 25%"));
		for (int32 I = 0; I < 3; ++I)
		{
			R.Step();
		}
		PoseAt(*this, R.Step(), GameplayLoc, TEXT("back on lock-on"));

		// The pending return blend never leaks into a later, unrelated transition.
		FACFCameraRequestParams Other;
		Other.Priority = 20;
		R.Director->AddRequest(R.Fixed(LocB), Other);
		const FACFCameraEvaluationResult& Next = R.Step();
		TestFalse(TEXT("later switch uses its own cut"), Next.bIsBlending);
		PoseAt(*this, Next, LocB, TEXT("later switch"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceLocalPlayersTest, "ActionCameraFramework.Sequence.LocalPlayersIndependent", ACFTests::Flags)
bool FACFSequenceLocalPlayersTest::RunTest(const FString& Parameters)
{
	FRig P1;
	FRig P2;
	P1.Gameplay();
	P2.Gameplay();
	UACFCameraSequence* Sequence = P1.NewSequence(); // one shared asset
	P1.AddShot(Sequence, TEXT("A"), LocA, 1.f);
	P1.AddShot(Sequence, TEXT("B"), LocB, 1.f);
	const FACFCameraSequenceHandle H1 = P1.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	const FACFCameraSequenceHandle H2 = P2.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
	TestTrue(TEXT("handles are unique across players"), H1 != H2);
	P1.Step();
	P2.Step();

	P1.Director->SendSequenceCommand(H1, EACFCameraSequenceCommand::AdvanceShot);
	TestFalse(TEXT("player 2 rejects player 1's handle"), P2.Director->SendSequenceCommand(H1, EACFCameraSequenceCommand::Cancel));
	PoseAt(*this, P1.Step(), LocB, TEXT("player 1 advanced"));
	PoseAt(*this, P2.Step(), LocA, TEXT("player 2 unaffected"));
	P1.Director->SendSequenceCommand(H1, EACFCameraSequenceCommand::HoldShot);
	for (int32 I = 0; I < 4; ++I)
	{
		P1.Step();
		P2.Step();
	}
	TestEqual(TEXT("player 1 held"), P1.State(H1).ShotTime, Dt, 1e-4f);
	TestEqual(TEXT("player 2 time runs"), P2.State(H2).ShotTime, 6.f * Dt, 1e-4f);
	P1.Director->SendSequenceCommand(H1, EACFCameraSequenceCommand::Cancel);
	P2.Step();
	TestTrue(TEXT("player 2 still playing after player 1 cancelled"), P2.Director->IsSequencePlaying(H2));
	TestEqual(TEXT("player 2 events only its own"), P2.Count(EACFCameraSequenceEventType::SequenceEnded), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
