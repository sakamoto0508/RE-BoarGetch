#include "ACFCameraZoneRequests.h"
#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraTuning.h"
#include "ACFCameraZoneComponent.h"
#include "GameFramework/Actor.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"

namespace ACFCameraZoneRequestImpl
{
	/** Does the preset's camera take its rotation from the control rotation (an Orbital Follow with that input source)? */
	static bool UsesControlRotation(const UACFCameraPreset* Preset)
	{
		if (!Preset)
		{
			return false;
		}
		for (const UACFCameraNode* Node : Preset->Nodes)
		{
			const UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(Node);
			if (Orbit && Orbit->bEnabled && Orbit->InputSource == EACFCameraOrbitInputSource::ControlRotation)
			{
				return true;
			}
		}
		return false;
	}

	/** Would a new request at Priority be the one in control (no request above it)? */
	static bool WouldBeBest(UACFCameraDirector& Director, int32 Priority)
	{
		const FACFCameraRequestHandle Best = Director.GetBestRequest();
		if (!Best.IsValid())
		{
			return true;
		}
		TArray<FACFCameraRequestInfo> Infos;
		Director.GetRequestInfos(Infos);
		const FACFCameraRequestInfo* Info = Infos.FindByPredicate([Best](const FACFCameraRequestInfo& Entry) { return Entry.Handle == Best; });
		return !Info || Info->Priority <= Priority; // equal priority: the newest (this one) wins
	}

	static FACFCameraTarget MakeTarget(AActor* Actor)
	{
		FACFCameraTarget Target;
		if (IsValid(Actor))
		{
			Target.Actor = Actor;
		}
		return Target;
	}
}

const double FACFCameraZoneRequestState::MinRetryDelay = 0.1;

EACFCameraZoneRequestEvent FACFCameraZoneRequestState::Update(const FACFCameraZoneRequestContext& Context, UACFCameraZoneComponent* Selected)
{
	UACFCameraDirector* Director = Context.Director;
	if (!Director)
	{
		return EACFCameraZoneRequestEvent::None;
	}
	EACFCameraZoneRequestEvent Event = EACFCameraZoneRequestEvent::None;

	// 1. Lifetime of the owned request: the game may have released it (Release All Cameras), or its instigator went away.
	if (Handle.IsValid() && !Director->HasRequest(Handle))
	{
		Handle.Invalidate();
		++NumLost;
		// Never less than MinRetryDelay: a request released by the game every frame is not created again every frame.
		const double First = FMath::Max(static_cast<double>(Context.RetryDelay), MinRetryDelay);
		const double Delay = FMath::Min(First * FMath::Pow(2.0, static_cast<double>(FMath::Min(NumLost - 1, 16))), FMath::Max(static_cast<double>(Context.RetryMaxDelay), First));
		NextRetryTime = Context.Time + Delay;
		bWaitingForRetry = true;
		Event = EACFCameraZoneRequestEvent::Lost;
		UE_LOG(LogActionCamera, Log, TEXT("ACF Camera Zone %s: its camera request disappeared (loss %d); requested again in %.2f s if the zone stays selected."),
			*GetNameSafe(ActiveZone.IsValid() ? ActiveZone->GetOwner() : nullptr), NumLost, Delay);
	}

	const bool bSameZone = Selected && bHasActiveZone && ActiveZone.Get() == Selected;
	if (bSameZone)
	{
		if (Handle.IsValid())
		{
			if (NumLost > 0 && Context.Time - RequestTime >= Context.RetryMaxDelay)
			{
				NumLost = 0; // the request lived long enough: the next loss starts from the first delay again
			}
			if (FollowTarget.Actor != Context.FollowTarget.Actor || FollowTarget.Component != Context.FollowTarget.Component)
			{
				// Another pawn (possession, respawn): keep the request, change its targets.
				FollowTarget = Context.FollowTarget;
				Director->SetRequestTargets(Handle, Context.FollowTarget, ACFCameraZoneRequestImpl::MakeTarget(Selected->LookAtTarget));
			}
			TrackViewWhileHidden(Context);
		}
		else if (bWaitingForRetry && Context.Time >= NextRetryTime)
		{
			bWaitingForRetry = false;
			const bool bRequested = Request(Context, *Selected);
			if (bRequested)
			{
				WarnSamePriority(Context);
			}
			Event = bRequested ? EACFCameraZoneRequestEvent::Retried : EACFCameraZoneRequestEvent::Failed;
		}
		return Event;
	}

	// 2. The selection changed: request the new zone first (so a zone-to-zone switch blends with the new Blend In), then
	// release the old request.
	const FACFCameraRequestHandle Old = Handle;
	const UACFCameraZoneComponent* OldZone = ActiveZone.Get();
	const bool bHadZone = bHasActiveZone;
	ActiveZone = Selected;
	bHasActiveZone = Selected != nullptr;
	Handle.Invalidate();
	NumLost = 0;
	bWaitingForRetry = false;
	bFailed = false;
	bTrackViewUntilShown = false;
	const bool bRequested = Selected && Request(Context, *Selected);
	if (Old.IsValid() && Director->HasRequest(Old))
	{
		if (bRequested)
		{
			if (Context.Camera)
			{
				FACFCameraRequestHandle Release = Old;
				Context.Camera->ReleaseCamera(Release);
			}
			else
			{
				Director->RemoveRequest(Old);
			}
		}
		else
		{
			Leave(Context, Old, OldZone);
		}
	}
	if (bRequested)
	{
		WarnSamePriority(Context); // after the old zone request is gone: only game requests are left to compare with
	}
	if (Selected)
	{
		return bRequested ? (bHadZone ? EACFCameraZoneRequestEvent::Switched : EACFCameraZoneRequestEvent::Requested) : EACFCameraZoneRequestEvent::Failed;
	}
	return bHadZone ? EACFCameraZoneRequestEvent::Released : Event;
}

bool FACFCameraZoneRequestState::Request(const FACFCameraZoneRequestContext& Context, UACFCameraZoneComponent& Zone)
{
	UACFCameraDirector* Director = Context.Director;
	UACFCameraPreset* Preset = Zone.GetPresetOrWarn();
	if (!Preset)
	{
		bFailed = true;
		return false;
	}
	FACFCameraRequestParams Params;
	Params.Priority = Context.Priority;
	Params.Instigator = &Zone; // a zone that goes away without EndPlay still takes its request with it
	Params.bOverrideBlendIn = Zone.bOverrideBlendIn;
	Params.BlendIn = Zone.BlendIn;
	if (Context.bCut && ACFCameraZoneRequestImpl::WouldBeBest(*Director, Context.Priority))
	{
		// Teleport: no blend across the level. Only when this request takes the screen now; below a Lock-On / Finisher the
		// zone keeps its Blend In for when it is revealed.
		Params.bOverrideBlendIn = true;
		Params.BlendIn = FACFCameraBlendSettings::MakeCut();
	}
	Params.FollowTarget = Context.FollowTarget;
	Params.LookAtTarget = ACFCameraZoneRequestImpl::MakeTarget(Zone.LookAtTarget); // empty if unset or destroyed
	Params.DebugName = FName(*FString::Printf(TEXT("Zone %s"), *GetNameSafe(Zone.GetOwner())));
	Handle = Context.Camera ? Context.Camera->RequestCamera(Preset, Params) : Director->AddRequest(Preset, Params);
	if (!Handle.IsValid())
	{
		bFailed = true;
		return false;
	}
	FollowTarget = Context.FollowTarget;
	RequestTime = Context.Time;

	// Rotation handoff to the zone's own request only. When it takes the screen now: from the view on screen. When it is
	// hidden (a Lock-On / Finisher above it, or a Sequencer camera owns the screen): it follows the view on screen until it is
	// first shown (7C-2B), so it appears where the screen was looking whenever the camera above it ends.
	bTrackViewUntilShown = false;
	if (Zone.EnterRotation == EACFCameraZoneRotationHandoff::KeepView)
	{
		if (Director->GetBestRequest() == Handle && !Director->IsViewOwnedExternally())
		{
			HandOffRotation(Context, Handle);
		}
		else
		{
			bTrackViewUntilShown = true;
			TrackViewWhileHidden(Context);
		}
	}
	return true;
}

void FACFCameraZoneRequestState::TrackViewWhileHidden(const FACFCameraZoneRequestContext& Context)
{
	if (!bTrackViewUntilShown || !Handle.IsValid())
	{
		return;
	}
	UACFCameraDirector* Director = Context.Director;
	if (Director->GetActiveRequest() == Handle && !Director->IsViewOwnedExternally())
	{
		bTrackViewUntilShown = false; // it was on screen in the last camera update: from now on it keeps its own orbit
		return;
	}
	if (!Context.bHasViewRotation || Context.ViewRotation.ContainsNaN())
	{
		return;
	}
	if (ACFCameraZoneRequestImpl::UsesControlRotation(Director->GetRequestPreset(Handle)))
	{
		// Never the control rotation while another camera has the screen: that would change the gameplay under a Lock-On.
		return;
	}
	// Only this request's orbit; a hidden rig is not evaluated, the last value is applied when it is first shown.
	Director->SetOrbitRotation(Handle, FRotator(Context.ViewRotation.Pitch, Context.ViewRotation.Yaw, 0.0), /*bInstant*/ true);
}

void FACFCameraZoneRequestState::WarnSamePriority(const FACFCameraZoneRequestContext& Context)
{
	if (bWarnedSamePriority)
	{
		return;
	}
	// The priority contract (README 32 章): a game request with the zone priority fights the zone ("the most recent wins").
	// This player has no other zone request at this point (the previous one was released before this check).
	TArray<FACFCameraRequestInfo> Infos;
	Context.Director->GetRequestInfos(Infos);
	if (const FACFCameraRequestInfo* Same = Infos.FindByPredicate([this, &Context](const FACFCameraRequestInfo& Info) { return Info.Handle != Handle && Info.Priority == Context.Priority; }))
	{
		bWarnedSamePriority = true;
		UE_LOG(LogActionCamera, Warning, TEXT("ACF Camera Zone: request %s has the zone request priority %d; it competes with the zone cameras (the most recent wins). Use a lower priority for exploration cameras and a higher one for cameras that must win over zones."),
			*Same->DebugName.ToString(), Context.Priority);
	}
}

void FACFCameraZoneRequestState::Leave(const FACFCameraZoneRequestContext& Context, FACFCameraRequestHandle Old, const UACFCameraZoneComponent* OldZone)
{
	UACFCameraDirector* Director = Context.Director;
	// On screen: the active request, and still the best one (no request above it arrived this frame).
	const bool bOnScreen = Director->GetActiveRequest() == Old && Director->GetBestRequest() == Old;
	FACFCameraRequestHandle Release = Old;
	if (!bOnScreen)
	{
		// Hidden below a Lock-On / Finisher (or not shown yet): the screen does not change, nothing else is touched.
		if (Context.Camera)
		{
			Context.Camera->ReleaseCamera(Release);
		}
		else
		{
			Director->RemoveRequest(Old);
		}
		return;
	}
	const FACFCameraBlendSettings BlendOut = Context.bCut ? FACFCameraBlendSettings::MakeCut() : OldZone ? OldZone->GetExitBlend() : FACFCameraBlendSettings();
	// Any other live request is below the zone (the zone is the best one): it takes over.
	const bool bRequestBelow = Director->GetNumRequests() > 1;
	if (bRequestBelow)
	{
		// The exploration camera takes over with the zone's Blend Out (instead of its own Blend In).
		if (Context.Camera)
		{
			Context.Camera->ReleaseCameraWithReturnBlend(Release, BlendOut);
		}
		else
		{
			Director->RemoveRequest(Old, &BlendOut, /*bOverrideAnyReturn*/ true);
		}
		// 7C-2B: not while a Sequencer camera owns the screen (its view is not the zone camera's).
	if ((!OldZone || OldZone->ExitRotation == EACFCameraZoneRotationHandoff::KeepView) && !Director->IsViewOwnedExternally())
		{
			HandOffRotation(Context, Director->GetBestRequest());
		}
	}
	else
	{
		// Back to the UE camera with the zone's Blend Out.
		if (Context.Camera)
		{
			Context.Camera->ReleaseCameraWithBlend(Release, BlendOut);
		}
		else
		{
			Director->RemoveRequest(Old, &BlendOut);
		}
	}
}

void FACFCameraZoneRequestState::HandOffRotation(const FACFCameraZoneRequestContext& Context, FACFCameraRequestHandle Target)
{
	if (!Context.bHasViewRotation || !Target.IsValid() || Context.ViewRotation.ContainsNaN())
	{
		return;
	}
	const FRotator View(Context.ViewRotation.Pitch, Context.ViewRotation.Yaw, 0.0);
	if (ACFCameraZoneRequestImpl::UsesControlRotation(Context.Director->GetRequestPreset(Target)))
	{
		// The orbit of such a camera follows the control rotation every frame: setting the orbit would not hold.
		if (Context.SetControlRotation)
		{
			Context.SetControlRotation(View);
		}
		return;
	}
	Context.Director->SetOrbitRotation(Target, View, /*bInstant*/ true); // false (nothing changed) without Orbital Follow
}

void FACFCameraZoneRequestState::Release(UACFCameraDirector* Director)
{
	if (Director && Handle.IsValid())
	{
		Director->RemoveRequest(Handle);
	}
	Handle.Invalidate();
	ActiveZone.Reset();
	bHasActiveZone = false;
	bTrackViewUntilShown = false;
	NumLost = 0;
	bWaitingForRetry = false;
	bFailed = false;
}
