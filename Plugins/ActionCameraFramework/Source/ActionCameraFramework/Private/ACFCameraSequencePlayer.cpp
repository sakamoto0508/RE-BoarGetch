#include "ACFCameraSequencePlayer.h"
#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSettings.h"
#include "Effects/ACFCameraEffect.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "ACFCameraProfiler.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ACFCameraSequence"

namespace ACFSequence
{
	/** Process-wide so a stale handle never controls a newer playback (even of another player). */
	static int32 GNextSequenceId = 0;

	static int32 AllocateId()
	{
		if (GNextSequenceId == MAX_int32)
		{
			GNextSequenceId = 0;
		}
		return ++GNextSequenceId;
	}

	static void Log(const FString& Message)
	{
		if (GetDefault<UACFCameraSettings>()->bLogRequests)
		{
			UE_LOG(LogActionCamera, Log, TEXT("%s"), *Message);
		}
		else
		{
			UE_LOG(LogActionCamera, Verbose, TEXT("%s"), *Message);
		}
	}

	static const TArray<FACFCameraShot>& Shots(const FACFCameraSequencePlayback& Playback)
	{
		static const TArray<FACFCameraShot> Empty;
		return Playback.Sequence ? Playback.Sequence->Shots : Empty;
	}

	static AActor* RoleActor(const FACFCameraSequencePlayback& Playback, EACFCameraShotRole Role)
	{
		switch (Role)
		{
		case EACFCameraShotRole::Attacker: return Playback.Params.Attacker.Get();
		case EACFCameraShotRole::Target: return Playback.Params.Target.Get();
		default: return nullptr;
		}
	}

	static void SetRoleTarget(FACFCameraTarget& Out, const FACFCameraSequencePlayback& Playback, EACFCameraShotRole Role, FName Socket)
	{
		Out = FACFCameraTarget();
		if (Role == EACFCameraShotRole::None)
		{
			return;
		}
		// Keep the weak pointer even if the actor is already gone, so nodes see a "lost" target (not "unset").
		Out.Actor = Role == EACFCameraShotRole::Attacker ? Playback.Params.Attacker : Playback.Params.Target;
		Out.SocketName = Socket;
	}

	/** Effects already finished on their own are dropped (keeps the owned list short; no allocation). */
	static void PruneEffects(UACFCameraDirector& Director, FACFCameraSequencePlayback& Playback)
	{
		Playback.Effects.RemoveAll([&Director](const FACFCameraSequencePlayback::FOwnedEffect& Effect)
		{
			return !Director.IsEffectActive(Effect.Handle);
		});
	}
}

// -------------------------------------------------------------------------------------------------
// Sequence asset validation

#if WITH_EDITOR
EDataValidationResult UACFCameraSequence::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Shots.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("NoShots", "The sequence has no shots; playing it ends immediately."));
	}
	TSet<FName> Names;
	for (int32 Index = 0; Index < Shots.Num(); ++Index)
	{
		const FACFCameraShot& Shot = Shots[Index];
		if (!Shot.Preset)
		{
			Context.AddError(FText::Format(LOCTEXT("NoPreset", "Shot {0} has no camera preset."), Index));
			Result = EDataValidationResult::Invalid;
		}
		if (!Shot.Name.IsNone())
		{
			bool bDuplicate = false;
			Names.Add(Shot.Name, &bDuplicate);
			if (bDuplicate)
			{
				Context.AddWarning(FText::Format(LOCTEXT("DupName", "Shot name '{0}' is used twice; Jump To Shot picks the first."), FText::FromName(Shot.Name)));
			}
		}
		auto HasCollision = [](const UACFCameraPreset* Preset)
		{
			if (!Preset)
			{
				return true;
			}
			for (const UACFCameraNode* Node : Preset->Nodes)
			{
				if (Node && Node->bEnabled && Node->GetStage() == EACFCameraNodeStage::PostBlend)
				{
					return true;
				}
			}
			return false;
		};
		if (!HasCollision(Shot.Preset) || !HasCollision(Shot.Fallback.FallbackPreset))
		{
			Context.AddWarning(FText::Format(LOCTEXT("NoCollision", "Shot {0}: a preset has no Collision node, so the output guard cannot keep this shot out of walls."), Index));
		}
		if (Shot.AdvanceMode == EACFCameraShotAdvanceMode::WaitForEvent && Shot.Duration <= 0.f)
		{
			Context.AddWarning(FText::Format(LOCTEXT("NoTimeout", "Shot {0} waits for an event without a timeout; a missing anim notify leaves the camera on this shot until Finish / Cancel."), Index));
		}
	}
	return Result;
}
#endif

// -------------------------------------------------------------------------------------------------
// Commands

FACFCameraSequenceHandle FACFCameraSequencePlayer::Play(UACFCameraDirector& Director, const UACFCameraSequence* Sequence, const FACFCameraSequenceParams& Params)
{
	if (!Sequence)
	{
		UE_LOG(LogActionCamera, Warning, TEXT("PlayCameraSequence rejected: sequence is null."));
		return FACFCameraSequenceHandle();
	}
	if (Params.bStopOtherSequences)
	{
		for (int32 Index = Playbacks.Num() - 1; Index >= 0; --Index)
		{
			// The new playback takes over directly: no return blend for the replaced one.
			EndPlayback(Director, Index, EACFCameraSequenceEndReason::Replaced, nullptr, false);
		}
	}

	FACFCameraSequencePlayback& Playback = Playbacks.AddDefaulted_GetRef();
	Playback.Handle = FACFCameraSequenceHandle(ACFSequence::AllocateId());
	Playback.Sequence = Sequence;
	Playback.Params = Params;
	Playback.PlayRate = FMath::Max(Params.PlayRate, 0.f);
	Playback.bTracksAttacker = !Params.Attacker.IsExplicitlyNull();
	Playback.bTracksTarget = !Params.Target.IsExplicitlyNull();
	Playback.bTracksInstigator = !Params.Instigator.IsExplicitlyNull();
	// The first shot starts on the next camera update (it needs the frame's context for the fallback probe), which
	// for a call from gameplay / an anim notify is the camera update of the same frame.
	ACFSequence::Log(FString::Printf(TEXT("PlaySequence %s '%s' attacker=%s target=%s"), *Playback.Handle.ToString(),
		*GetNameSafe(Sequence), *GetNameSafe(Params.Attacker.Get()), *GetNameSafe(Params.Target.Get())));
	return Playback.Handle;
}

bool FACFCameraSequencePlayer::SendCommand(UACFCameraDirector& Director, FACFCameraSequenceHandle Handle, EACFCameraSequenceCommand Command, FName ShotName)
{
	const int32 Index = FindIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	FACFCameraSequencePlayback& Playback = Playbacks[Index];
	switch (Command)
	{
	case EACFCameraSequenceCommand::AdvanceShot:
		if (Playback.PendingJump == INDEX_NONE)
		{
			Playback.bPendingAdvance = true; // several in one frame collapse into one
		}
		return true;
	case EACFCameraSequenceCommand::JumpToShot:
	{
		const int32 Target = ACFSequence::Shots(Playback).IndexOfByPredicate([ShotName](const FACFCameraShot& Shot) { return Shot.Name == ShotName; });
		if (Target == INDEX_NONE)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("%s: no shot named '%s'."), *Handle.ToString(), *ShotName.ToString());
			return false;
		}
		return JumpToShot(Handle, Target);
	}
	case EACFCameraSequenceCommand::HoldShot:
		++Playback.HoldCount;
		return true;
	case EACFCameraSequenceCommand::ReleaseShot:
		Playback.HoldCount = FMath::Max(Playback.HoldCount - 1, 0);
		return true;
	case EACFCameraSequenceCommand::Finish:
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::Advanced, Playback.Sequence ? &Playback.Sequence->ReturnBlend : nullptr, false);
		return true;
	case EACFCameraSequenceCommand::Cancel:
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::Cancelled, Playback.Sequence ? &Playback.Sequence->CancelBlend : nullptr, true);
		return true;
	}
	return false;
}

bool FACFCameraSequencePlayer::JumpToShot(FACFCameraSequenceHandle Handle, int32 ShotIndex)
{
	FACFCameraSequencePlayback* Playback = Find(Handle);
	if (!Playback || !ACFSequence::Shots(*Playback).IsValidIndex(ShotIndex))
	{
		return false;
	}
	Playback->PendingJump = ShotIndex; // last one wins
	Playback->bPendingAdvance = false;
	return true;
}

int32 FACFCameraSequencePlayer::SendCommandToActor(UACFCameraDirector& Director, const AActor* Actor, EACFCameraSequenceCommand Command, FName ShotName)
{
	// Collect first: Finish / Cancel remove playbacks.
	TArray<FACFCameraSequenceHandle, TInlineAllocator<4>> Matches;
	for (const FACFCameraSequencePlayback& Playback : Playbacks)
	{
		if (!Actor || Playback.Params.Attacker.Get() == Actor || Playback.Params.Target.Get() == Actor)
		{
			Matches.Add(Playback.Handle);
		}
	}
	int32 Count = 0;
	for (const FACFCameraSequenceHandle Handle : Matches)
	{
		Count += SendCommand(Director, Handle, Command, ShotName) ? 1 : 0;
	}
	return Count;
}

bool FACFCameraSequencePlayer::SetPlayRate(FACFCameraSequenceHandle Handle, float PlayRate)
{
	FACFCameraSequencePlayback* Playback = Find(Handle);
	if (!Playback)
	{
		return false;
	}
	Playback->PlayRate = FMath::Max(PlayRate, 0.f);
	return true;
}

bool FACFCameraSequencePlayer::GetState(const UACFCameraDirector& Director, FACFCameraSequenceHandle Handle, FACFCameraSequenceState& OutState) const
{
	OutState = FACFCameraSequenceState();
	const FACFCameraSequencePlayback* Playback = Find(Handle);
	if (!Playback)
	{
		return false;
	}
	const TArray<FACFCameraShot>& Shots = ACFSequence::Shots(*Playback);
	OutState.bIsPlaying = true;
	OutState.ShotIndex = Playback->ShotIndex;
	OutState.NumShots = Shots.Num();
	OutState.ShotTime = Playback->ShotTime;
	OutState.SequenceTime = Playback->SequenceTime;
	OutState.PlayRate = Playback->PlayRate;
	OutState.bHeld = Playback->HoldCount > 0;
	OutState.bUsingFallback = Playback->bUsingFallback;
	OutState.ShotRequest = Playback->Request;
	// Phase 5A: 出力抑制・出力バイパス中（Sequencer などが画面を握っている）は「握っていない」扱い。
	OutState.bInControl = Playback->Request.IsValid() && Director.GetActiveRequest() == Playback->Request && !Director.IsViewOwnedExternally();
	OutState.NumOwnedEffects = Playback->Effects.Num();
	if (Shots.IsValidIndex(Playback->ShotIndex))
	{
		const FACFCameraShot& Shot = Shots[Playback->ShotIndex];
		OutState.ShotName = Shot.Name;
		OutState.ShotDuration = Shot.Duration;
		OutState.bWaitingForEvent = Shot.AdvanceMode == EACFCameraShotAdvanceMode::WaitForEvent;
	}
	return true;
}

void FACFCameraSequencePlayer::StopAll(UACFCameraDirector& Director, bool bImmediate)
{
	for (int32 Index = Playbacks.Num() - 1; Index >= 0; --Index)
	{
		const UACFCameraSequence* Sequence = Playbacks[Index].Sequence;
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::Cancelled, bImmediate || !Sequence ? nullptr : &Sequence->CancelBlend, bImmediate);
	}
}

void FACFCameraSequencePlayer::ConsumeEvents(TArray<FACFCameraSequenceEvent>& Out)
{
	Out.Reset();
	Out.Append(Events); // both arrays keep their allocation
	Events.Reset();
}

// -------------------------------------------------------------------------------------------------
// Tick

void FACFCameraSequencePlayer::Tick(UACFCameraDirector& Director, const FACFCameraEvaluationContext& Context)
{
	if (Playbacks.IsEmpty())
	{
		return;
	}
	TGuardValue<bool> Guard(bTicking, true);
	for (int32 Index = Playbacks.Num() - 1; Index >= 0; --Index)
	{
		TickPlayback(Director, Index, Context);
	}
}

void FACFCameraSequencePlayer::TickPlayback(UACFCameraDirector& Director, int32 Index, const FACFCameraEvaluationContext& Context)
{
	FACFCameraSequencePlayback& Playback = Playbacks[Index];
	const UACFCameraSequence* Sequence = Playback.Sequence;
	if (!Sequence)
	{
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::Cancelled, nullptr, true);
		return;
	}

	// 1. Ownership checks: our request released by somebody else, owner / actors destroyed.
	if (Playback.Request.IsValid() && !Director.HasRequest(Playback.Request))
	{
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::RequestLost, nullptr, false);
		return;
	}
	if (Playback.bTracksInstigator && !Playback.Params.Instigator.IsValid())
	{
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::Cancelled, &Sequence->CancelBlend, true);
		return;
	}
	const bool bActorLost = (Playback.bTracksAttacker && !Playback.Params.Attacker.IsValid())
		|| (Playback.bTracksTarget && !Playback.Params.Target.IsValid());
	if (bActorLost && Sequence->ActorLostPolicy != EACFCameraSequenceActorLostPolicy::Continue)
	{
		const bool bCancel = Sequence->ActorLostPolicy == EACFCameraSequenceActorLostPolicy::Cancel;
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::ActorLost, bCancel ? &Sequence->CancelBlend : &Sequence->ReturnBlend, bCancel);
		return;
	}

	// 2. First shot / latched events.
	if (!Playback.bStarted)
	{
		Playback.bStarted = true;
		const int32 First = Playback.PendingJump != INDEX_NONE ? Playback.PendingJump : Playback.Params.StartShot;
		Playback.PendingJump = INDEX_NONE;
		if (!BeginShotChain(Director, Index, First, 0.f, Context))
		{
			return;
		}
	}
	// Latched events (an Advance sent before the first camera update applies right after the first shot started).
	if (Playbacks[Index].PendingJump != INDEX_NONE)
	{
		const int32 Target = Playback.PendingJump;
		Playback.PendingJump = INDEX_NONE;
		Playback.bPendingAdvance = false;
		EndCurrentShot(Director, Playback, EACFCameraSequenceEndReason::Skipped);
		if (!BeginShotChain(Director, Index, Target, 0.f, Context))
		{
			return;
		}
	}
	else if (Playback.bPendingAdvance)
	{
		Playback.bPendingAdvance = false;
		EndCurrentShot(Director, Playback, EACFCameraSequenceEndReason::Advanced);
		if (!BeginShotChain(Director, Index, Playback.ShotIndex + 1, 0.f, Context))
		{
			return;
		}
	}

	// 3. Interruption by a higher priority camera.
	FACFCameraSequencePlayback& Live = Playbacks[Index];
	// Phase 5A: 外部（Sequencer の Camera Cut、出力抑制）に画面を奪われた場合も、同じ Interrupt Policy に従う。
	const bool bInControl = Director.GetBestRequest() == Live.Request && !Context.bOutputBypassed && !Director.IsOutputSuppressed();
	if (!bInControl && Sequence->InterruptPolicy == EACFCameraSequenceInterruptPolicy::Cancel)
	{
		EndPlayback(Director, Index, EACFCameraSequenceEndReason::Interrupted, nullptr, true);
		return;
	}

	// 4. Time (game time; frozen while paused / held / interrupted with Pause).
	float Rate = Live.PlayRate;
	if (Sequence->bUseAttackerTimeDilation)
	{
		if (const AActor* Attacker = Live.Params.Attacker.Get())
		{
			Rate *= Attacker->CustomTimeDilation;
		}
	}
	const bool bPaused = Context.World && Context.World->IsPaused();
	const bool bFrozen = bPaused || Live.HoldCount > 0
		|| (!bInControl && Sequence->InterruptPolicy == EACFCameraSequenceInterruptPolicy::Pause);
	const float Step = bFrozen ? 0.f : FMath::Max(Context.DeltaTime, 0.f) * Rate;
	Live.ShotTime += Step;
	Live.SequenceTime += Step;

	// 5. Shots ending on their own (time-out). Overflow carries into the next shot so timing does not drift.
	const TArray<FACFCameraShot>& Shots = Sequence->Shots;
	if (Shots.IsValidIndex(Live.ShotIndex))
	{
		const FACFCameraShot& Shot = Shots[Live.ShotIndex];
		const bool bTimed = Shot.AdvanceMode == EACFCameraShotAdvanceMode::Duration || Shot.Duration > 0.f;
		if (bTimed && Live.ShotTime >= Shot.Duration && !bFrozen)
		{
			const float Carry = Live.ShotTime - Shot.Duration;
			EndCurrentShot(Director, Live, EACFCameraSequenceEndReason::Completed);
			BeginShotChain(Director, Index, Live.ShotIndex + 1, Carry, Context);
		}
	}
}

bool FACFCameraSequencePlayer::BeginShotChain(UACFCameraDirector& Director, int32 PlaybackIndex, int32 ShotIndex, float Carry, const FACFCameraEvaluationContext& Context)
{
	FACFCameraSequencePlayback& Playback = Playbacks[PlaybackIndex];
	const TArray<FACFCameraShot>& Shots = ACFSequence::Shots(Playback);
	int32 Index = FMath::Max(ShotIndex, 0);
	// Bounded: every iteration moves to the next shot.
	while (Index < Shots.Num())
	{
		const FACFCameraShot& Shot = Shots[Index];
		const bool bTimed = Shot.AdvanceMode == EACFCameraShotAdvanceMode::Duration || Shot.Duration > 0.f;
		if (bTimed && Carry >= Shot.Duration)
		{
			// A long frame (hitch) covered this whole shot: report it without creating its camera.
			Playback.ShotIndex = Index;
			PushEvent(Playback, EACFCameraSequenceEventType::ShotStarted, Index, EACFCameraSequenceEndReason::None);
			PushEvent(Playback, EACFCameraSequenceEventType::ShotEnded, Index, EACFCameraSequenceEndReason::Completed);
			Carry -= Shot.Duration;
			++Index;
			continue;
		}
		bool bInfeasible = false;
		if (StartShot(Director, Playback, Index, FMath::Min(Carry, Shot.Duration > 0.f ? Shot.Duration : Carry), Context, bInfeasible))
		{
			return true;
		}
		// Skipped (infeasible / no preset): report and continue with the next shot.
		PushEvent(Playback, EACFCameraSequenceEventType::ShotStarted, Index, EACFCameraSequenceEndReason::None);
		PushEvent(Playback, EACFCameraSequenceEventType::ShotEnded, Index, EACFCameraSequenceEndReason::Infeasible);
		Carry = 0.f;
		++Index;
	}
	// Past the last shot: the playback is complete (its last shot already reported its end).
	const UACFCameraSequence* Sequence = Playback.Sequence;
	EndPlayback(Director, PlaybackIndex, EACFCameraSequenceEndReason::Completed, Sequence ? &Sequence->ReturnBlend : nullptr, false, /*bShotEnded*/ true);
	return false;
}

bool FACFCameraSequencePlayer::StartShot(UACFCameraDirector& Director, FACFCameraSequencePlayback& Playback, int32 ShotIndex, float Carry, const FACFCameraEvaluationContext& Context, bool& bOutInfeasible)
{
	const FACFCameraShot& Shot = ACFSequence::Shots(Playback)[ShotIndex];
	bOutInfeasible = false;
	if (!Shot.Preset)
	{
		UE_LOG(LogActionCamera, Warning, TEXT("%s: shot %d has no preset, skipped."), *Playback.Handle.ToString(), ShotIndex);
		bOutInfeasible = true;
		return false;
	}

	const FACFCameraRequestParams ShotParams = MakeShotParams(Playback, Shot, ShotIndex);
	const UACFCameraPreset* Preset = Shot.Preset;
	bool bFallback = false;
	if (Shot.Fallback.NeedsProbe() && !IsShotFeasible(Director, Playback, Shot, ShotParams, Context))
	{
		if (Shot.Fallback.FallbackPreset)
		{
			Preset = Shot.Fallback.FallbackPreset;
			bFallback = true;
		}
		else
		{
			ACFSequence::Log(FString::Printf(TEXT("%s: shot %d '%s' infeasible here, skipped."), *Playback.Handle.ToString(), ShotIndex, *Shot.Name.ToString()));
			bOutInfeasible = true;
			return false;
		}
	}

	// New shot first, then the previous one goes (it stays alive only as the live source of the blend).
	const FACFCameraRequestHandle Previous = Playback.Request;
	Playback.Request = Director.AddRequest(Preset, ShotParams);
	if (Previous.IsValid())
	{
		Director.RemoveRequest(Previous);
	}
	Playback.ShotIndex = ShotIndex;
	Playback.ShotTime = FMath::Max(Carry, 0.f);
	Playback.bUsingFallback = bFallback;

	// Effects owned by the playback.
	ACFSequence::PruneEffects(Director, Playback);
	for (const FACFCameraShotEffect& Effect : Shot.Effects)
	{
		if (!Effect.Preset)
		{
			continue;
		}
		FACFCameraEffectParams EffectParams;
		EffectParams.Scale = Effect.Scale;
		EffectParams.SourceActor = Playback.Params.Target;
		EffectParams.TargetActor = Playback.Params.Attacker;
		const FACFCameraEffectHandle Handle = Director.PlayEffect(Effect.Preset, EffectParams);
		if (Handle.IsValid())
		{
			Playback.Effects.Add({ Handle, Effect.Lifetime });
		}
	}

	ACFSequence::Log(FString::Printf(TEXT("%s shot %d '%s' -> %s%s"), *Playback.Handle.ToString(), ShotIndex, *Shot.Name.ToString(),
		*Playback.Request.ToString(), bFallback ? TEXT(" (fallback)") : TEXT("")));
	PushEvent(Playback, EACFCameraSequenceEventType::ShotStarted, ShotIndex, EACFCameraSequenceEndReason::None, bFallback);
	return true;
}

void FACFCameraSequencePlayer::EndCurrentShot(UACFCameraDirector& Director, FACFCameraSequencePlayback& Playback, EACFCameraSequenceEndReason Reason)
{
	if (Playback.ShotIndex == INDEX_NONE)
	{
		return;
	}
	// Shot-lifetime effects are released (they fade out with their own release time).
	for (int32 Index = Playback.Effects.Num() - 1; Index >= 0; --Index)
	{
		if (Playback.Effects[Index].Lifetime == EACFCameraShotEffectLifetime::Shot)
		{
			Director.StopEffect(Playback.Effects[Index].Handle, false);
			Playback.Effects.RemoveAtSwap(Index);
		}
	}
	PushEvent(Playback, EACFCameraSequenceEventType::ShotEnded, Playback.ShotIndex, Reason);
}

void FACFCameraSequencePlayer::EndPlayback(UACFCameraDirector& Director, int32 Index, EACFCameraSequenceEndReason Reason, const FACFCameraBlendSettings* ReturnBlend, bool bCutEffects, bool bShotEnded)
{
	FACFCameraSequencePlayback Playback = MoveTemp(Playbacks[Index]);
	Playbacks.RemoveAt(Index);

	if (!bShotEnded && Playback.ShotIndex != INDEX_NONE && Playback.Request.IsValid())
	{
		PushEvent(Playback, EACFCameraSequenceEventType::ShotEnded, Playback.ShotIndex, Reason);
	}
	// No effect outlives the playback: everything still owned is released (or cut on cancel). Fire-and-forget
	// one-shots finish on their own unless the playback is cut.
	for (const FACFCameraSequencePlayback::FOwnedEffect& Effect : Playback.Effects)
	{
		if (bCutEffects || Effect.Lifetime != EACFCameraShotEffectLifetime::FireAndForget)
		{
			Director.StopEffect(Effect.Handle, bCutEffects);
		}
	}
	if (Playback.Request.IsValid())
	{
		// The return blend applies whatever camera takes over (lock-on, gameplay camera or the UE camera).
		Director.RemoveRequest(Playback.Request, ReturnBlend, /*bOverrideAnyReturn*/ ReturnBlend != nullptr);
	}
	ACFSequence::Log(FString::Printf(TEXT("%s ended (%s)"), *Playback.Handle.ToString(), *UEnum::GetValueAsString(Reason)));
	FACFCameraSequenceEvent& Event = Events.AddDefaulted_GetRef();
	Event.Sequence = Playback.Handle;
	Event.Type = EACFCameraSequenceEventType::SequenceEnded;
	Event.ShotIndex = Playback.ShotIndex;
	Event.Reason = Reason;
}

// -------------------------------------------------------------------------------------------------
// Helpers

FACFCameraRequestParams FACFCameraSequencePlayer::MakeShotParams(const FACFCameraSequencePlayback& Playback, const FACFCameraShot& Shot, int32 ShotIndex) const
{
	const UACFCameraSequence* Sequence = Playback.Sequence;
	FACFCameraRequestParams Params;
	Params.Priority = (Playback.Params.bOverridePriority ? Playback.Params.Priority : (Sequence ? Sequence->Priority : 1000)) + Shot.PriorityOffset;
	Params.bOverrideBlendIn = true;
	Params.BlendIn = Shot.BlendIn;
	Params.Instigator = Playback.Params.Instigator;
	ACFSequence::SetRoleTarget(Params.FollowTarget, Playback, Shot.Targets.Follow, Shot.Targets.FollowSocket);
	ACFSequence::SetRoleTarget(Params.LookAtTarget, Playback, Shot.Targets.LookAt, Shot.Targets.LookAtSocket);
	Params.bEnableDeoccluder = Shot.bEnableDeoccluder;
	auto AddMember = [&Params, &Shot](AActor* Actor, float Weight)
	{
		if (Actor && Weight > 0.f)
		{
			FACFCameraTargetGroupMember Member;
			Member.Target.Actor = Actor;
			Member.Weight = Weight;
			Member.Radius = Shot.Targets.GroupRadius;
			Params.TargetGroup.Members.Add(Member);
		}
	};
	AddMember(Playback.Params.Attacker.Get(), Shot.Targets.AttackerGroupWeight);
	AddMember(Playback.Params.Target.Get(), Shot.Targets.TargetGroupWeight);
	if (Sequence && Sequence->bCollisionIgnoresRoleActors)
	{
		for (AActor* Actor : { Playback.Params.Attacker.Get(), Playback.Params.Target.Get() })
		{
			if (Actor)
			{
				Params.CollisionIgnoredActors.Add(Actor);
			}
		}
	}
	Params.DebugName = FName(*FString::Printf(TEXT("%s.%s"), *GetNameSafe(Sequence), Shot.Name.IsNone() ? *FString::FromInt(ShotIndex) : *Shot.Name.ToString()));
	return Params;
}

bool FACFCameraSequencePlayer::IsShotFeasible(UACFCameraDirector& Director, const FACFCameraSequencePlayback& Playback, const FACFCameraShot& Shot, const FACFCameraRequestParams& ShotParams, const FACFCameraEvaluationContext& Context) const
{
	FACFCameraRequestParams ProbeParams = ShotParams;
	ProbeParams.bEnableDeoccluder = false; // judge the staged composition itself
	FACFCameraProbeResult Probe;
	if (!Director.ProbeCamera(Shot.Preset, ProbeParams, Context, Probe) || !Probe.bHasConstraint)
	{
		return true; // nothing to judge (no world / no Collision node)
	}
	const double Desired = FVector::Dist(Probe.Pivot, Probe.DesiredPose.Location);
	const double Actual = FVector::Dist(Probe.Pivot, Probe.Pose.Location);
	if (Desired > 1.0 && Actual / Desired < Shot.Fallback.MinDistanceRatio)
	{
		ACFSequence::Log(FString::Printf(TEXT("%s shot '%s' infeasible: collision keeps %.0f%% of the distance"), *Playback.Handle.ToString(), *Shot.Name.ToString(), 100.0 * Actual / Desired));
		return false;
	}
	if (Shot.Fallback.bRequireLineOfSight && Context.World)
	{
		ACF_PROFILE_SCOPE(ShotProbe);
		ACF_PROFILE_PURPOSE(ShotProbe);
		AActor* Attacker = Playback.Params.Attacker.Get();
		AActor* Target = Playback.Params.Target.Get();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ACFShotProbe), false);
		for (const AActor* Ignored : Probe.IgnoredActors)
		{
			if (Ignored != Attacker && Ignored != Target)
			{
				Params.AddIgnoredActor(Ignored);
			}
		}
		for (const AActor* Subject : { static_cast<const AActor*>(Attacker), static_cast<const AActor*>(Target) })
		{
			if (!Subject)
			{
				continue;
			}
			FHitResult Hit;
			ACF_PROFILE_QUERY(Line);
			if (Context.World->LineTraceSingleByChannel(Hit, Probe.Pose.Location, Subject->GetActorLocation(), Probe.Channel, Params)
				&& Hit.GetActor() != Subject && Hit.GetActor() != Attacker && Hit.GetActor() != Target)
			{
				ACFSequence::Log(FString::Printf(TEXT("%s shot '%s' infeasible: %s hidden by %s"), *Playback.Handle.ToString(), *Shot.Name.ToString(), *GetNameSafe(Subject), *GetNameSafe(Hit.GetActor())));
				return false;
			}
		}
	}
	return true;
}

void FACFCameraSequencePlayer::PushEvent(const FACFCameraSequencePlayback& Playback, EACFCameraSequenceEventType Type, int32 ShotIndex, EACFCameraSequenceEndReason Reason, bool bFallback)
{
	FACFCameraSequenceEvent& Event = Events.AddDefaulted_GetRef();
	Event.Sequence = Playback.Handle;
	Event.Type = Type;
	Event.ShotIndex = ShotIndex;
	const TArray<FACFCameraShot>& Shots = ACFSequence::Shots(Playback);
	Event.ShotName = Shots.IsValidIndex(ShotIndex) ? Shots[ShotIndex].Name : NAME_None;
	Event.Reason = Reason;
	Event.bFallback = bFallback;
}

FACFCameraSequencePlayback* FACFCameraSequencePlayer::Find(FACFCameraSequenceHandle Handle)
{
	const int32 Index = FindIndex(Handle);
	return Index != INDEX_NONE ? &Playbacks[Index] : nullptr;
}

const FACFCameraSequencePlayback* FACFCameraSequencePlayer::Find(FACFCameraSequenceHandle Handle) const
{
	const int32 Index = FindIndex(Handle);
	return Index != INDEX_NONE ? &Playbacks[Index] : nullptr;
}

int32 FACFCameraSequencePlayer::FindIndex(FACFCameraSequenceHandle Handle) const
{
	return Handle.IsValid() ? Playbacks.IndexOfByPredicate([Handle](const FACFCameraSequencePlayback& Playback) { return Playback.Handle == Handle; }) : INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
