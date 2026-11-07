#include "ACFCameraDirector.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSettings.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"

namespace ACFDirector
{
	/** Process-wide so a stale handle can never alias a newer request (even across directors). */
	static int32 GNextRequestId = 0;

	static int32 AllocateRequestId()
	{
		if (GNextRequestId == MAX_int32)
		{
			GNextRequestId = 0;
		}
		return ++GNextRequestId;
	}

	static void LogEvent(const FString& Message)
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

	static FString Describe(const FACFCameraRequestEntry& Entry)
	{
		return FString::Printf(TEXT("%s '%s' preset=%s priority=%d"),
			*Entry.Handle.ToString(), *Entry.Params.DebugName.ToString(),
			*GetNameSafe(Entry.Rig.GetPreset()), Entry.Params.Priority);
	}
}

// -------------------------------------------------------------------------------------------------
// Requests

FACFCameraRequestHandle UACFCameraDirector::AddRequest(const UACFCameraPreset* Preset, const FACFCameraRequestParams& Params)
{
	if (!Preset)
	{
		UE_LOG(LogActionCamera, Warning, TEXT("AddRequest '%s' rejected: preset is null."), *Params.DebugName.ToString());
		return FACFCameraRequestHandle();
	}

	FACFCameraRequestEntry& Entry = Requests.AddDefaulted_GetRef();
	Entry.Handle = FACFCameraRequestHandle(ACFDirector::AllocateRequestId());
	Entry.Params = Params;
	Entry.Rig.Initialize(Preset);
	Entry.Sequence = NextSequence++;
	// "Explicitly null" means no instigator was supplied. An already-dead instigator still counts
	// as tracked, so such a request is pruned on the next evaluation instead of living forever.
	Entry.bTracksInstigator = !Params.Instigator.IsExplicitlyNull();

	ACFDirector::LogEvent(FString::Printf(TEXT("AddRequest %s"), *ACFDirector::Describe(Entry)));
	return Entry.Handle;
}

bool UACFCameraDirector::RemoveRequest(FACFCameraRequestHandle Handle, const FACFCameraBlendSettings* BlendOutOverride)
{
	const int32 Index = FindRequestIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	ACFDirector::LogEvent(FString::Printf(TEXT("RemoveRequest %s"), *ACFDirector::Describe(Requests[Index])));
	if (Handle == ActiveHandle || Handle == Transition.LiveSource)
	{
		// Still on screen (active, or the live outgoing side of a blend): keep its rig alive until
		// the blend that replaces it has finished.
		RetireActiveEntry(Index, Handle == ActiveHandle ? BlendOutOverride : nullptr);
	}
	else
	{
		Requests.RemoveAt(Index);
	}
	return true;
}

void UACFCameraDirector::RemoveAllRequests(bool bImmediate)
{
	if (bImmediate)
	{
		Requests.Reset();
		RetiredRequests.Reset();
		ActiveHandle.Invalidate();
		Transition = FTransition();
		LastResult.Weight = 0.f;
		LastResult.RequestWeight = 0.f;
		LastResult.ActiveRequest.Invalidate();
		LastResult.bIsBlending = false;
		LastResult.BlendAlpha = 1.f;
		LastResult.NumRequests = 0;
		LastEvaluatedFrame = MAX_uint64; // the next evaluation in this frame must not reuse the cache
		OutputConstraint.bValid = false;
	}
	else
	{
		for (int32 Index = Requests.Num() - 1; Index >= 0; --Index)
		{
			const FACFCameraRequestHandle Handle = Requests[Index].Handle;
			if (Handle == ActiveHandle || Handle == Transition.LiveSource)
			{
				RetireActiveEntry(Index, nullptr);
			}
		}
		Requests.Reset();
	}
	ACFDirector::LogEvent(FString::Printf(TEXT("RemoveAllRequests immediate=%d"), bImmediate ? 1 : 0));
}

bool UACFCameraDirector::SetRequestTargets(FACFCameraRequestHandle Handle, const FACFCameraTarget& FollowTarget, const FACFCameraTarget& LookAtTarget)
{
	FACFCameraRequestEntry* Entry = FindLiveEntry(Handle);
	if (!Entry)
	{
		return false;
	}
	// Nodes notice the identity change and reset their damping (no slide across the level).
	Entry->Params.FollowTarget = FollowTarget;
	Entry->Params.LookAtTarget = LookAtTarget;
	return true;
}

bool UACFCameraDirector::SetRequestLookAtTarget(FACFCameraRequestHandle Handle, const FACFCameraTarget& LookAtTarget)
{
	FACFCameraRequestEntry* Entry = FindLiveEntry(Handle);
	if (!Entry)
	{
		return false;
	}
	Entry->Params.LookAtTarget = LookAtTarget;
	ACFDirector::LogEvent(FString::Printf(TEXT("SetLookAtTarget %s -> %s"), *ACFDirector::Describe(*Entry),
		*GetNameSafe(LookAtTarget.Component.IsValid() ? static_cast<const UObject*>(LookAtTarget.Component.Get()) : LookAtTarget.Actor.Get())));
	return true;
}

bool UACFCameraDirector::AddGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTargetGroupMember& Member)
{
	FACFCameraRequestEntry* Entry = FindLiveEntry(Handle);
	if (!Entry || !Member.Target.IsSet())
	{
		return false;
	}
	Entry->Params.TargetGroup.AddOrUpdate(Member);
	return true;
}

bool UACFCameraDirector::RemoveGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTarget& Target)
{
	FACFCameraRequestEntry* Entry = FindLiveEntry(Handle);
	return Entry && Entry->Params.TargetGroup.Remove(Target);
}

bool UACFCameraDirector::SetGroupMembers(FACFCameraRequestHandle Handle, const TArray<FACFCameraTargetGroupMember>& Members)
{
	FACFCameraRequestEntry* Entry = FindLiveEntry(Handle);
	if (!Entry)
	{
		return false;
	}
	Entry->Params.TargetGroup.Members = Members;
	return true;
}

const FACFCameraTargetGroup* UACFCameraDirector::GetGroup(FACFCameraRequestHandle Handle) const
{
	auto Matches = [Handle](const FACFCameraRequestEntry& Entry) { return Entry.Handle == Handle; };
	const FACFCameraRequestEntry* Entry = Handle.IsValid() ? Requests.FindByPredicate(Matches) : nullptr;
	if (!Entry && Handle.IsValid())
	{
		Entry = RetiredRequests.FindByPredicate(Matches);
	}
	return Entry ? &Entry->Params.TargetGroup : nullptr;
}

bool UACFCameraDirector::HasRequest(FACFCameraRequestHandle Handle) const
{
	return FindRequestIndex(Handle) != INDEX_NONE;
}

// -------------------------------------------------------------------------------------------------
// Output suppression

void UACFCameraDirector::SuppressOutput(const UObject* Source, float BlendTime)
{
	if (!Source)
	{
		UE_LOG(LogActionCamera, Warning, TEXT("SuppressOutput ignored: source is null."));
		return;
	}
	if (SuppressionSources.Contains(Source))
	{
		return;
	}
	SuppressionSources.Add(Source);
	if (SuppressionSources.Num() == 1)
	{
		bSuppressionDirty = true;
		PendingSuppressionBlendTime = BlendTime;
	}
	ACFDirector::LogEvent(FString::Printf(TEXT("SuppressOutput by %s (blend %.2fs)"), *GetNameSafe(Source), BlendTime));
}

bool UACFCameraDirector::ReleaseOutputSuppression(const UObject* Source, float BlendTime)
{
	if (SuppressionSources.Remove(Source) == 0)
	{
		return false;
	}
	if (SuppressionSources.IsEmpty())
	{
		bSuppressionDirty = true;
		PendingSuppressionBlendTime = BlendTime;
	}
	ACFDirector::LogEvent(FString::Printf(TEXT("ReleaseOutputSuppression by %s (blend %.2fs)"), *GetNameSafe(Source), BlendTime));
	return true;
}

void UACFCameraDirector::PruneLostSuppressionSources()
{
	const int32 Before = SuppressionSources.Num();
	SuppressionSources.RemoveAll([](const TWeakObjectPtr<const UObject>& Source) { return !Source.IsValid(); });
	if (Before > 0 && SuppressionSources.IsEmpty())
	{
		// The owner (e.g. a sequence actor) vanished without releasing: hand control back at once.
		bSuppressionDirty = true;
		PendingSuppressionBlendTime = 0.f;
		ACFDirector::LogEvent(TEXT("Output suppression released (source lost)"));
	}
}

void UACFCameraDirector::StartSuppressionFade(float BlendTime)
{
	SuppressionFade.From = SuppressionFade.Value;
	SuppressionFade.To = SuppressionSources.IsEmpty() ? 1.f : 0.f;
	SuppressionFade.Duration = FMath::Max(BlendTime, 0.f);
	SuppressionFade.Elapsed = 0.f;
	if (SuppressionFade.Duration <= 0.f)
	{
		SuppressionFade.Value = SuppressionFade.To;
	}
}

float UACFCameraDirector::AdvanceSuppressionFade(float DeltaTime)
{
	FFade& Fade = SuppressionFade;
	if (Fade.Value != Fade.To)
	{
		Fade.Elapsed += FMath::Max(DeltaTime, 0.f);
		const float T = Fade.Duration > 0.f ? Fade.Elapsed / Fade.Duration : 1.f;
		Fade.Value = T >= 1.f ? Fade.To : FMath::Lerp(Fade.From, Fade.To, FMath::InterpEaseInOut(0.f, 1.f, T, 2.f));
	}
	return Fade.Value;
}

// -------------------------------------------------------------------------------------------------
// Evaluation

const FACFCameraEvaluationResult& UACFCameraDirector::Evaluate(const FACFCameraEvaluationContext& InContext)
{
	const bool bSameFrame = InContext.FrameNumber == LastEvaluatedFrame;
	const bool bUpgradeZeroDelta = bSameFrame && LastEvaluatedDeltaTime <= 0.f && InContext.DeltaTime > 0.f;
	if (bSameFrame && !bUpgradeZeroDelta)
	{
		return LastResult;
	}
	LastRigEvaluationCount = 0;

	// Stage 1 (input): camera input is consumed only by an evaluation that advances time, so a
	// zero-delta call (InitializeFor) followed by the real one in the same frame never loses or
	// doubles it, and cached repeat calls never see it again.
	FACFCameraEvaluationContext Context = InContext;
	if (Context.DeltaTime > 0.f)
	{
		Context.CameraInput = PendingInputDelta + InputRate * Context.DeltaTime;
		PendingInputDelta = FVector2D::ZeroVector;
		InputRate = FVector2D::ZeroVector;
	}
	else
	{
		Context.CameraInput = FVector2D::ZeroVector;
	}

	// Stage 2: resolve which request is in control, and who owns the output.
	PruneLostInstigators();
	PruneLostSuppressionSources();
	if (bSuppressionDirty)
	{
		StartSuppressionFade(PendingSuppressionBlendTime);
		bSuppressionDirty = false;
	}
	const int32 BestIndex = FindBestRequestIndex();
	const FACFCameraRequestHandle BestHandle = BestIndex != INDEX_NONE ? Requests[BestIndex].Handle : FACFCameraRequestHandle();
	if (BestHandle != ActiveHandle)
	{
		BeginTransition(BestIndex, Context);
	}

	// Stage 4 (time): advance the transition once for this frame.
	const float Alpha = AdvanceTransition(Context.DeltaTime);
	FACFCameraRequestEntry* LiveSource = Transition.bActive ? FindLiveEntry(Transition.LiveSource) : nullptr;

	// Stage 3: evaluate the rig(s) (Body + Aim), seeded with the upstream UE pose.
	FACFCameraRigScratch TargetScratch;
	FACFCameraRigScratch SourceScratch;
	FACFCameraRequestEntry* TargetEntry = nullptr;
	FACFCameraPose TargetPose;
	if (BestIndex != INDEX_NONE)
	{
		TargetEntry = &Requests[BestIndex];
		TargetPose = EvaluateEntry(*TargetEntry, Context, TargetScratch);
	}
	else if (LiveSource)
	{
		// Blending back to UE with a live source: the released camera keeps moving while it fades.
		TargetEntry = LiveSource;
		TargetPose = EvaluateEntry(*LiveSource, Context, TargetScratch);
	}
	else
	{
		TargetPose = LastBlendedPose; // blending back to UE from a snapshot: hold the last pose (without effects)
	}

	// Stage 4 (mix): request-to-request blend.
	FACFCameraEvaluationResult Result;
	const bool bBlendingPose = Transition.bActive && Transition.bBlendPose;
	if (bBlendingPose)
	{
		const bool bLiveBlend = LiveSource && LiveSource != TargetEntry;
		const FACFCameraPose SourcePose = bLiveBlend ? EvaluateEntry(*LiveSource, Context, SourceScratch) : Transition.FromPose;
		Result.Pose = FACFCameraPose::Blend(SourcePose, TargetPose, Alpha);
		if (bLiveBlend && SourceScratch.bHasPivot && TargetScratch.bHasPivot)
		{
			// Collision of a blended camera sweeps from the blended pivot.
			TargetScratch.Pivot = FMath::Lerp(SourceScratch.Pivot, TargetScratch.Pivot, static_cast<double>(Alpha));
		}
	}
	else
	{
		Result.Pose = TargetPose;
	}

	// Stage 5a: effects. Evaluated once per frame (inside the frame guard); location before Collision.
	LastBlendedPose = Result.Pose;
	const FACFCameraPose EffectCamera = Result.Pose;
	Effects.Evaluate(Context, EffectCamera);
	Effects.ApplyPreCollision(Result.Pose, EffectCamera);

	// Stage 5 (corrections of the final pose): PostBlend nodes (Collision) of the camera in control.
	// Phase 3 visual effects will be inserted before this so they cannot push the camera into walls.
	if (TargetEntry && TargetEntry->Rig.HasPostBlendNodes())
	{
		const FACFCameraNodeContext PostContext(Context, TargetEntry->Params);
		PostContext.Scratch = TargetScratch;
		TargetEntry->Rig.EvaluatePostBlend(PostContext, Result.Pose);
		TargetScratch = PostContext.Scratch;
	}
	OutputConstraint.bValid = TargetScratch.Constraint.bValid;
	if (OutputConstraint.bValid)
	{
		OutputConstraint.Pivot = TargetScratch.Constraint.Pivot;
		OutputConstraint.ProbeRadius = TargetScratch.Constraint.ProbeRadius;
		OutputConstraint.Channel = TargetScratch.Constraint.Channel;
		OutputConstraint.IgnoredActors.Reset();
		for (const AActor* Actor : TargetScratch.Constraint.IgnoredActors)
		{
			OutputConstraint.IgnoredActors.Add(Actor);
		}
	}
	// Stage 5c: rotation / FOV effects after Collision (they do not move the camera). The aim pose excludes them.
	const FACFCameraPose AimSource = Result.Pose;
	const UACFCameraSettings* Settings = GetDefault<UACFCameraSettings>();
	Effects.ApplyPostCollision(Result.Pose, Settings->EffectFovMin, Settings->EffectFovMax);
#if !UE_BUILD_SHIPPING
	LastDebug = TargetScratch.Debug;
#endif

	// Stage 6: final result.
	Result.RequestWeight = Transition.bActive ? FMath::Lerp(Transition.FromWeight, Transition.ToWeight, Alpha) : Transition.ToWeight;
	const float OutputFactor = AdvanceSuppressionFade(Context.DeltaTime);
	Result.Weight = Result.RequestWeight * OutputFactor;
	Result.bOutputSuppressed = IsOutputSuppressed();
	Result.UpstreamPose = Context.UpstreamPose;
	Result.AimPose = FACFCameraPose::Blend(Context.UpstreamPose, AimSource, Result.Weight);
	Result.ActiveRequest = ActiveHandle;
	Result.bIsBlending = Transition.bActive;
	Result.BlendAlpha = Transition.bActive ? Alpha : 1.f;
	Result.NumRequests = Requests.Num();
	Result.FrameNumber = Context.FrameNumber;

	LastResult = Result;
	LastEvaluatedFrame = Context.FrameNumber;
	LastEvaluatedDeltaTime = Context.DeltaTime;
	return LastResult;
}

FACFCameraPose UACFCameraDirector::EvaluateEntry(FACFCameraRequestEntry& Entry, const FACFCameraEvaluationContext& Context, FACFCameraRigScratch& OutScratch)
{
	FACFCameraPose Pose = Context.UpstreamPose;
	const FACFCameraNodeContext NodeContext(Context, Entry.Params);
	Entry.Rig.Evaluate(NodeContext, Pose);
	OutScratch = NodeContext.Scratch;
	++LastRigEvaluationCount;
	return Pose;
}

// -------------------------------------------------------------------------------------------------
// Input / runtime commands

void UACFCameraDirector::AddCameraInput(const FVector2D& Delta)
{
	PendingInputDelta += Delta;
}

void UACFCameraDirector::SetCameraInputRate(const FVector2D& Rate)
{
	InputRate = Rate;
}

void UACFCameraDirector::NotifyTeleport(bool bCutBlend)
{
	for (FACFCameraRequestEntry& Entry : Requests)
	{
		Entry.Rig.ResetInterpolation();
	}
	for (FACFCameraRequestEntry& Entry : RetiredRequests)
	{
		Entry.Rig.ResetInterpolation();
	}
	if (bCutBlend && Transition.bActive)
	{
		EndTransition(); // do not blend across the level from the pre-teleport pose
	}
	ACFDirector::LogEvent(FString::Printf(TEXT("NotifyTeleport cutBlend=%d"), bCutBlend ? 1 : 0));
}

bool UACFCameraDirector::SetOrbitRotation(FACFCameraRequestHandle Handle, const FRotator& Rotation, bool bInstant)
{
	FACFCameraRequestEntry* Entry = FindLiveEntry(Handle);
	if (!Entry)
	{
		return false;
	}
	bool bFound = false;
	Entry->Rig.ForEachNode([&](const UACFCameraNode& Node, FInstancedStruct* State)
	{
		if (const UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(&Node))
		{
			Orbit->SetOrbitRotation(State, Rotation, bInstant);
			bFound = true;
		}
	});
	return bFound;
}

void UACFCameraDirector::PruneLostInstigators()
{
	// Destroyed group members are dropped (RemoveAll keeps the allocation: no per-frame heap traffic).
	for (FACFCameraRequestEntry& Entry : Requests)
	{
		Entry.Params.TargetGroup.RemoveLostMembers();
	}
	for (FACFCameraRequestEntry& Entry : RetiredRequests)
	{
		Entry.Params.TargetGroup.RemoveLostMembers();
	}

	for (int32 Index = Requests.Num() - 1; Index >= 0; --Index)
	{
		const FACFCameraRequestEntry& Entry = Requests[Index];
		bool bLookAtLost = false;
		if (Entry.Params.bReleaseWhenLookAtTargetLost && Entry.Params.LookAtTarget.IsSet())
		{
			FTransform Unused;
			const UObject* Object = nullptr;
			bLookAtLost = !Entry.Params.LookAtTarget.Resolve(Unused, Object);
		}
		if ((Entry.bTracksInstigator && !Entry.Params.Instigator.IsValid()) || bLookAtLost)
		{
			ACFDirector::LogEvent(FString::Printf(TEXT("Auto-release (%s lost) %s"), bLookAtLost ? TEXT("look-at target") : TEXT("instigator"), *ACFDirector::Describe(Entry)));
			if (Entry.Handle == ActiveHandle || Entry.Handle == Transition.LiveSource)
			{
				RetireActiveEntry(Index, nullptr);
			}
			else
			{
				Requests.RemoveAt(Index);
			}
		}
	}
}

int32 UACFCameraDirector::FindBestRequestIndex() const
{
	int32 BestIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		const FACFCameraRequestEntry& Candidate = Requests[Index];
		if (BestIndex == INDEX_NONE)
		{
			BestIndex = Index;
			continue;
		}
		const FACFCameraRequestEntry& Best = Requests[BestIndex];
		if (Candidate.Params.Priority > Best.Params.Priority
			|| (Candidate.Params.Priority == Best.Params.Priority && Candidate.Sequence > Best.Sequence))
		{
			BestIndex = Index;
		}
	}
	return BestIndex;
}

int32 UACFCameraDirector::FindRequestIndex(FACFCameraRequestHandle Handle) const
{
	if (!Handle.IsValid())
	{
		return INDEX_NONE;
	}
	return Requests.IndexOfByPredicate([Handle](const FACFCameraRequestEntry& Entry) { return Entry.Handle == Handle; });
}

FACFCameraRequestEntry* UACFCameraDirector::FindLiveEntry(FACFCameraRequestHandle Handle)
{
	if (!Handle.IsValid())
	{
		return nullptr;
	}
	auto Matches = [Handle](const FACFCameraRequestEntry& Entry) { return Entry.Handle == Handle; };
	if (FACFCameraRequestEntry* Found = Requests.FindByPredicate(Matches))
	{
		return Found;
	}
	return RetiredRequests.FindByPredicate(Matches);
}

void UACFCameraDirector::RetireActiveEntry(int32 Index, const FACFCameraBlendSettings* BlendOutOverride)
{
	FACFCameraRequestEntry& Entry = Requests[Index];
	if (Entry.Handle == ActiveHandle)
	{
		if (BlendOutOverride)
		{
			PendingBlendOut = *BlendOutOverride;
		}
		else if (const UACFCameraPreset* Preset = Entry.Rig.GetPreset())
		{
			PendingBlendOut = Preset->DefaultBlendOut;
		}
		else
		{
			PendingBlendOut = FACFCameraBlendSettings();
		}
	}
	RetiredRequests.Add(MoveTemp(Entry));
	Requests.RemoveAt(Index);
}

void UACFCameraDirector::BeginTransition(int32 NewActiveIndex, const FACFCameraEvaluationContext& Context)
{
	const bool bInterrupting = Transition.bActive;
	const bool bHadAcfInfluence = LastResult.RequestWeight > 0.f;
	const FACFCameraRequestHandle PreviousActive = ActiveHandle;

	// Start from whatever ACF produces right now (possibly mid-blend), so interruptions never pop.
	Transition.FromPose = LastBlendedPose; // without effects, which are re-applied on top every frame
	Transition.FromWeight = LastResult.RequestWeight;
	Transition.Elapsed = 0.f;
	Transition.LiveSource.Invalidate();

	if (NewActiveIndex != INDEX_NONE)
	{
		FACFCameraRequestEntry& Entry = Requests[NewActiveIndex];
		const UACFCameraPreset* Preset = Entry.Rig.GetPreset();
		TransitionBlend = Entry.Params.bOverrideBlendIn ? Entry.Params.BlendIn : (Preset ? Preset->DefaultBlendIn : FACFCameraBlendSettings());
		Transition.ToWeight = 1.f;
		// Without previous ACF influence the "from" side is the live upstream camera, which is
		// handled by RequestWeight alone; blending the pose too would blend against stale data.
		Transition.bBlendPose = bHadAcfInfluence;

		const FACFCameraNodeContext NodeContext(Context, Entry.Params);
		Entry.Rig.Activate(NodeContext);
		ActiveHandle = Entry.Handle;
		ACFDirector::LogEvent(FString::Printf(TEXT("Activate %s (blend %.2fs)"), *ACFDirector::Describe(Entry), TransitionBlend.IsCut() ? 0.f : TransitionBlend.Duration));
	}
	else
	{
		TransitionBlend = PendingBlendOut;
		Transition.ToWeight = 0.f;
		Transition.bBlendPose = false;
		ActiveHandle.Invalidate();
		ACFDirector::LogEvent(FString::Printf(TEXT("Return to UE camera (blend %.2fs)"), TransitionBlend.IsCut() ? 0.f : TransitionBlend.Duration));
	}

	// Live source: the camera that was fully in control keeps being evaluated during the blend.
	// Interrupting a blend always uses the snapshot (the on-screen pose) instead.
	if (TransitionBlend.Source == EACFCameraBlendSource::Live && !TransitionBlend.IsCut()
		&& !bInterrupting && bHadAcfInfluence && FindLiveEntry(PreviousActive))
	{
		Transition.LiveSource = PreviousActive;
	}

	// Retired rigs that are not the live source of this blend are no longer needed.
	const FACFCameraRequestHandle Keep = Transition.LiveSource;
	RetiredRequests.RemoveAll([Keep](const FACFCameraRequestEntry& Entry) { return Entry.Handle != Keep; });

	PendingBlendOut = FACFCameraBlendSettings();
	Transition.bActive = !TransitionBlend.IsCut();
	if (!Transition.bActive)
	{
		EndTransition();
	}
}

float UACFCameraDirector::AdvanceTransition(float DeltaTime)
{
	if (!Transition.bActive)
	{
		return 1.f;
	}

	Transition.Elapsed += FMath::Max(DeltaTime, 0.f);
	const float Duration = TransitionBlend.Duration;
	if (Transition.Elapsed >= Duration)
	{
		EndTransition();
		return 1.f;
	}
	return TransitionBlend.EvaluateAlpha(Transition.Elapsed / Duration);
}

void UACFCameraDirector::EndTransition()
{
	Transition.bActive = false;
	Transition.LiveSource.Invalidate();
	RetiredRequests.Reset();
}
