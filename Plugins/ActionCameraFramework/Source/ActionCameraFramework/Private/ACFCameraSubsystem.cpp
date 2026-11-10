#include "ACFCameraSubsystem.h"
#include "ACFCameraDirector.h"
#include "ACFCameraOutputModifier.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSettings.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Debug/DebugDrawService.h"
#include "ACFCameraDebugDraw.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Effects/ACFCameraBuiltinEffects.h"

void UACFCameraSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Director = NewObject<UACFCameraDirector>(this, TEXT("ACFCameraDirector"));
	OutputModifierClass = GetDefault<UACFCameraSettings>()->ResolveOutputModifierClass();
	WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::HandleWorldTickStart);
#if !UE_BUILD_SHIPPING
	DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &ThisClass::HandleDebugDraw));
#endif
}

void UACFCameraSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
#if !UE_BUILD_SHIPPING
	UDebugDrawService::Unregister(DebugDrawHandle);
	DebugDrawHandle.Reset();
#endif
	DetachOutput();
	if (Director)
	{
		Director->RemoveAllRequests(true);
	}
	Super::Deinitialize();
}

void UACFCameraSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// PCM is spawned in APlayerController::PostInitializeComponents, which runs before the
	// controller is handed to the local player, so it is available here.
	if (Director && Director->IsInfluencingOutput())
	{
		EnsureOutputAttached();
	}
}

void UACFCameraSubsystem::HandleWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (!Director || !Director->IsInfluencingOutput())
	{
		return;
	}
	const APlayerCameraManager* CameraManager = GetCurrentCameraManager();
	if (!CameraManager || CameraManager->GetWorld() != World)
	{
		return; // another world (other PIE instance, editor world) or no camera yet
	}
	EnsureOutputAttached();
}

FACFCameraRequestHandle UACFCameraSubsystem::RequestCamera(UACFCameraPreset* Preset, const FACFCameraRequestParams& Params)
{
	if (!Director)
	{
		return FACFCameraRequestHandle();
	}
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset, Params);
	if (Handle.IsValid())
	{
		EnsureOutputAttached();
	}
	return Handle;
}

bool UACFCameraSubsystem::ReleaseCamera(FACFCameraRequestHandle& Handle)
{
	const bool bRemoved = Director && Director->RemoveRequest(Handle);
	Handle.Invalidate();
	return bRemoved;
}

bool UACFCameraSubsystem::ReleaseCameraWithBlend(FACFCameraRequestHandle& Handle, const FACFCameraBlendSettings& BlendOut)
{
	const bool bRemoved = Director && Director->RemoveRequest(Handle, &BlendOut);
	Handle.Invalidate();
	return bRemoved;
}

void UACFCameraSubsystem::ReleaseAllCameras(bool bImmediate)
{
	if (Director)
	{
		Director->RemoveAllRequests(bImmediate);
	}
}

bool UACFCameraSubsystem::IsCameraRequestAlive(FACFCameraRequestHandle Handle) const
{
	return Director && Director->HasRequest(Handle);
}

bool UACFCameraSubsystem::IsActiveCamera(FACFCameraRequestHandle Handle) const
{
	return Director && Handle.IsValid() && Director->GetActiveRequest() == Handle;
}

FACFCameraEvaluationResult UACFCameraSubsystem::GetLastEvaluationResult() const
{
	return Director ? Director->GetLastResult() : FACFCameraEvaluationResult();
}

bool UACFCameraSubsystem::SetCameraTargets(FACFCameraRequestHandle Handle, const FACFCameraTarget& FollowTarget, const FACFCameraTarget& LookAtTarget)
{
	return Director && Director->SetRequestTargets(Handle, FollowTarget, LookAtTarget);
}

bool UACFCameraSubsystem::SetCameraLockOnTarget(FACFCameraRequestHandle Handle, const FACFCameraTarget& LockTarget)
{
	return Director && Director->SetRequestLookAtTarget(Handle, LockTarget);
}

bool UACFCameraSubsystem::SetCameraLockOnActor(FACFCameraRequestHandle Handle, AActor* LockTarget)
{
	FACFCameraTarget Target;
	Target.Actor = LockTarget;
	return SetCameraLockOnTarget(Handle, Target);
}

bool UACFCameraSubsystem::AddCameraGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTargetGroupMember& Member)
{
	return Director && Director->AddGroupMember(Handle, Member);
}

bool UACFCameraSubsystem::AddCameraGroupActor(FACFCameraRequestHandle Handle, AActor* Actor, float Weight, float Radius)
{
	FACFCameraTargetGroupMember Member;
	Member.Target.Actor = Actor;
	Member.Weight = FMath::Max(Weight, 0.f);
	Member.Radius = FMath::Max(Radius, 0.f);
	return Actor && AddCameraGroupMember(Handle, Member);
}

bool UACFCameraSubsystem::RemoveCameraGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTarget& Target)
{
	return Director && Director->RemoveGroupMember(Handle, Target);
}

bool UACFCameraSubsystem::RemoveCameraGroupActor(FACFCameraRequestHandle Handle, AActor* Actor)
{
	FACFCameraTarget Target;
	Target.Actor = Actor;
	return Actor && RemoveCameraGroupMember(Handle, Target);
}

bool UACFCameraSubsystem::SetCameraGroupMembers(FACFCameraRequestHandle Handle, const TArray<FACFCameraTargetGroupMember>& Members)
{
	return Director && Director->SetGroupMembers(Handle, Members);
}

TArray<FACFCameraTargetGroupMember> UACFCameraSubsystem::GetCameraGroupMembers(FACFCameraRequestHandle Handle) const
{
	const FACFCameraTargetGroup* Group = Director ? Director->GetGroup(Handle) : nullptr;
	return Group ? Group->Members : TArray<FACFCameraTargetGroupMember>();
}

void UACFCameraSubsystem::SuppressOutput(UObject* Source, float BlendTime)
{
	if (Director)
	{
		Director->SuppressOutput(Source, BlendTime);
	}
}

bool UACFCameraSubsystem::ReleaseOutputSuppression(UObject* Source, float BlendTime)
{
	return Director && Director->ReleaseOutputSuppression(Source, BlendTime);
}

bool UACFCameraSubsystem::IsOutputSuppressed() const
{
	return Director && Director->IsOutputSuppressed();
}

FACFCameraPose UACFCameraSubsystem::GetAimCameraPose() const
{
	if (Director && Director->GetLastResult().FrameNumber != 0)
	{
		return Director->GetLastResult().AimPose;
	}
	// ACF has never evaluated (no request yet): the gameplay camera is the visual UE camera.
	return GetVisualCameraPose();
}

FACFCameraPose UACFCameraSubsystem::GetVisualCameraPose() const
{
	FACFCameraPose Pose;
	if (const APlayerCameraManager* CameraManager = GetCurrentCameraManager())
	{
		const FMinimalViewInfo& View = CameraManager->GetCameraCacheView();
		Pose.Location = View.Location;
		Pose.Rotation = View.Rotation;
		Pose.FieldOfView = View.FOV;
	}
	return Pose;
}

const FACFCameraEvaluationResult& UACFCameraSubsystem::EvaluateForOutput(float DeltaTime, const FMinimalViewInfo& UpstreamPOV)
{
	check(Director);

	// Stage 1: context collection.
	FACFCameraEvaluationContext Context;
	Context.DeltaTime = DeltaTime;
	Context.FrameNumber = GFrameCounter;
	Context.UpstreamPose.Location = UpstreamPOV.Location;
	Context.UpstreamPose.Rotation = UpstreamPOV.Rotation;
	Context.UpstreamPose.FieldOfView = UpstreamPOV.FOV;
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (APlayerController* PlayerController = LocalPlayer ? LocalPlayer->PlayerController.Get() : nullptr)
	{
		Context.PlayerController = PlayerController;
		Context.World = PlayerController->GetWorld();
		Context.ControlRotation = PlayerController->GetControlRotation();
		Context.bHasControlRotation = true;
	}
	// Aspect ratio for screen-space composition: constrained camera aspect, else this player's view.
	if (UpstreamPOV.bConstrainAspectRatio && UpstreamPOV.AspectRatio > 0.f)
	{
		Context.AspectRatio = UpstreamPOV.AspectRatio;
	}
	else if (LocalPlayer && LocalPlayer->ViewportClient)
	{
		FVector2D ViewportSize = FVector2D::ZeroVector;
		LocalPlayer->ViewportClient->GetViewportSize(ViewportSize);
		ViewportSize *= LocalPlayer->Size; // split screen
		if (ViewportSize.X > 0.0 && ViewportSize.Y > 0.0)
		{
			Context.AspectRatio = static_cast<float>(ViewportSize.X / ViewportSize.Y);
		}
	}

	// Stages 2-6 (input is consumed inside, once per time-advancing evaluation).
	const FACFCameraEvaluationResult& Result = Director->Evaluate(Context);
	FlushSequenceEvents();
#if ENABLE_DRAW_DEBUG && !UE_BUILD_SHIPPING
	if (Result.FrameNumber != LastDebugDrawFrame && ACFCameraDebug::IsWorldDrawEnabled())
	{
		LastDebugDrawFrame = Result.FrameNumber;
		ACFCameraDebug::DrawWorld(Context.World, Director->GetLastDebug(), Result, Director->GetLastSolverReport());
	}
#endif
	return Result;
}

APlayerCameraManager* UACFCameraSubsystem::GetCurrentCameraManager() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const APlayerController* PlayerController = LocalPlayer ? LocalPlayer->PlayerController.Get() : nullptr;
	return PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
}

bool UACFCameraSubsystem::AttachModifier(APlayerCameraManager* CameraManager, TSubclassOf<UACFCameraModifierBase> ModifierClass, TWeakObjectPtr<UACFCameraModifierBase>& InOutModifier)
{
	if (UACFCameraModifierBase* Existing = InOutModifier.Get())
	{
		// Fast path: still in the current PCM's list (someone may have removed it).
		if (Existing->GetOwningCameraManager() == CameraManager && CameraManager->FindCameraModifierByClass(Existing->GetClass()) == Existing)
		{
			return true;
		}
		if (APlayerCameraManager* OldManager = Existing->GetOwningCameraManager(); IsValid(OldManager) && OldManager != CameraManager)
		{
			OldManager->RemoveCameraModifier(Existing);
		}
		Existing->SetSubsystem(nullptr);
		InOutModifier.Reset();
	}

	UACFCameraModifierBase* Modifier = Cast<UACFCameraModifierBase>(CameraManager->FindCameraModifierByClass(ModifierClass));
	if (Modifier && Modifier->GetSubsystem() && Modifier->GetSubsystem() != this)
	{
		Modifier = nullptr;
	}
	if (!Modifier)
	{
		Modifier = Cast<UACFCameraModifierBase>(CameraManager->AddNewCameraModifier(ModifierClass));
	}
	if (!Modifier)
	{
		UE_LOG(LogActionCamera, Error, TEXT("Failed to add %s to %s (exclusive priority conflict?)."), *GetNameSafe(ModifierClass), *GetNameSafe(CameraManager));
		return false;
	}
	Modifier->SetSubsystem(this);
	InOutModifier = Modifier;
	UE_LOG(LogActionCamera, Log, TEXT("%s attached to %s (priority %d)."), *Modifier->GetName(), *CameraManager->GetName(), Modifier->Priority);
	return true;
}

void UACFCameraSubsystem::EnsureOutputAttached()
{
	APlayerCameraManager* CameraManager = GetCurrentCameraManager();
	if (!CameraManager || !IsValid(CameraManager))
	{
		// Retried from PlayerControllerChanged / the next world tick once a camera manager exists.
		UE_LOG(LogActionCamera, Verbose, TEXT("No PlayerCameraManager yet for %s; output attach deferred."), *GetNameSafe(GetLocalPlayer()));
		return;
	}
	if (!OutputModifierClass)
	{
		OutputModifierClass = GetDefault<UACFCameraSettings>()->ResolveOutputModifierClass();
	}
	if (!AttachModifier(CameraManager, OutputModifierClass.Get(), OutputModifier))
	{
		UE_LOG(LogActionCamera, Error, TEXT("ACF output disabled for %s."), *GetNameSafe(CameraManager));
		return;
	}
	// The guard is always attached with the output; whether it acts is decided per call by the setting.
	AttachModifier(CameraManager, UACFCameraCollisionGuardModifier::StaticClass(), GuardModifier);
}

void UACFCameraSubsystem::DetachOutput()
{
	for (TWeakObjectPtr<UACFCameraModifierBase>* Slot : { &OutputModifier, &GuardModifier })
	{
		if (UACFCameraModifierBase* Modifier = Slot->Get())
		{
			Modifier->SetSubsystem(nullptr);
			if (APlayerCameraManager* CameraManager = Modifier->GetOwningCameraManager(); IsValid(CameraManager))
			{
				CameraManager->RemoveCameraModifier(Modifier);
			}
		}
		Slot->Reset();
	}
}

bool UACFCameraSubsystem::ApplyOutputCollisionGuard(FVector& InOutLocation)
{
	if (!Director || !GetDefault<UACFCameraSettings>()->bEnableOutputCollisionGuard || !Director->GetLastResult().HasInfluence())
	{
		return false;
	}
	const FACFCameraOutputConstraint& Constraint = Director->GetOutputConstraint();
	const APlayerCameraManager* CameraManager = GetCurrentCameraManager();
	const UWorld* World = CameraManager ? CameraManager->GetWorld() : nullptr;
	if (!Constraint.bValid || !World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ACFCameraOutputGuard), false);
	for (const TWeakObjectPtr<const AActor>& Actor : Constraint.IgnoredActors)
	{
		if (const AActor* Ignored = Actor.Get())
		{
			Params.AddIgnoredActor(Ignored);
		}
	}
	FHitResult Hit;
	if (!ACFCameraCollision::SweepCamera(*World, Constraint.Pivot, InOutLocation, Constraint.ProbeRadius, Constraint.Channel, Params, Hit))
	{
		return false;
	}
	LastGuardCorrection = FVector::Dist(InOutLocation, Hit.Location);
	LastGuardFrame = GFrameCounter;
	InOutLocation = Hit.Location;
	return true;
}

void UACFCameraSubsystem::AddCameraInput(FVector2D YawPitchDelta)
{
	if (Director)
	{
		Director->AddCameraInput(YawPitchDelta);
	}
}

void UACFCameraSubsystem::SetCameraInputRate(FVector2D YawPitchRate)
{
	if (Director)
	{
		Director->SetCameraInputRate(YawPitchRate);
	}
}

bool UACFCameraSubsystem::SetOrbitRotation(FACFCameraRequestHandle Handle, FRotator Rotation, bool bInstant)
{
	return Director && Director->SetOrbitRotation(Handle, Rotation, bInstant);
}

void UACFCameraSubsystem::NotifyTeleport(bool bCutBlend)
{
	if (Director)
	{
		Director->NotifyTeleport(bCutBlend);
	}
}

#if !UE_BUILD_SHIPPING
void UACFCameraSubsystem::HandleDebugDraw(UCanvas* Canvas, APlayerController* PlayerController)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!Director || !LocalPlayer || PlayerController != LocalPlayer->PlayerController || !ACFCameraDebug::IsScreenDrawEnabled())
	{
		return;
	}
	TArray<FACFCameraEffectInfo> Effects;
	Director->GetActiveEffects(Effects);
	TArray<FString> Lines;
	TArray<FACFCameraSequenceHandle> Sequences;
	Director->GetActiveSequences(Sequences);
	for (const FACFCameraSequenceHandle Sequence : Sequences)
	{
		FACFCameraSequenceState S;
		Director->GetSequenceState(Sequence, S);
		Lines.Add(FString::Printf(TEXT("Sequence %s  shot %d/%d '%s'  t %.2f/%.2f  rate %.2f%s%s%s%s  effects %d"), *Sequence.ToString(), S.ShotIndex, S.NumShots,
			*S.ShotName.ToString(), S.ShotTime, S.ShotDuration, S.PlayRate, S.bWaitingForEvent ? TEXT("  (wait event)") : TEXT(""), S.bHeld ? TEXT("  HELD") : TEXT(""),
			S.bUsingFallback ? TEXT("  FALLBACK") : TEXT(""), S.bInControl ? TEXT("") : TEXT("  (not in control)"), S.NumOwnedEffects));
	}
	ACFCameraDebug::DrawCanvas(Canvas, Director->GetLastDebug(), Director->GetLastResult(), Director->GetLastSolverReport(), Effects, Lines);
}
#endif

// -------------------------------------------------------------------------------------------------
// Camera effects

FACFCameraEffectHandle UACFCameraSubsystem::PlayCameraEffect(UACFCameraEffectPreset* Preset, const FACFCameraEffectParams& Params)
{
	return Director ? Director->PlayEffect(Preset, Params) : FACFCameraEffectHandle();
}

bool UACFCameraSubsystem::StopCameraEffect(FACFCameraEffectHandle& Handle, bool bImmediate)
{
	const bool bStopped = Director && Director->StopEffect(Handle, bImmediate);
	Handle.Invalidate();
	return bStopped;
}

void UACFCameraSubsystem::StopAllCameraEffects(bool bImmediate)
{
	if (Director)
	{
		Director->StopAllEffects(bImmediate);
	}
}

UACFCameraEffectPreset* UACFCameraSubsystem::GetDefaultEffectPreset(bool bImpulse)
{
	const UACFCameraSettings* Settings = GetDefault<UACFCameraSettings>();
	if (UACFCameraEffectPreset* Configured = (bImpulse ? Settings->DefaultImpulsePreset : Settings->DefaultRecoilPreset).LoadSynchronous())
	{
		return Configured;
	}
	TObjectPtr<UACFCameraEffectPreset>& Builtin = bImpulse ? BuiltinImpulsePreset : BuiltinRecoilPreset;
	if (!Builtin)
	{
		Builtin = NewObject<UACFCameraEffectPreset>(this, bImpulse ? TEXT("ACFBuiltinImpulse") : TEXT("ACFBuiltinRecoil"), RF_Transient);
		if (bImpulse)
		{
			Builtin->Layers.Add(NewObject<UACFCameraEffect_Impulse>(Builtin));
		}
		else
		{
			Builtin->Layers.Add(NewObject<UACFCameraEffect_Recoil>(Builtin));
		}
	}
	return Builtin;
}

FACFCameraEffectHandle UACFCameraSubsystem::PlayCameraImpulse(FVector SourceLocation, float Scale, UACFCameraEffectPreset* Preset)
{
	FACFCameraEffectParams Params;
	Params.Scale = Scale;
	Params.bUseSourceLocation = true;
	Params.SourceLocation = SourceLocation;
	return PlayCameraEffect(Preset ? Preset : GetDefaultEffectPreset(true), Params);
}

FACFCameraEffectHandle UACFCameraSubsystem::PlayCameraRecoil(float Scale, UACFCameraEffectPreset* Preset)
{
	FACFCameraEffectParams Params;
	Params.Scale = Scale;
	return PlayCameraEffect(Preset ? Preset : GetDefaultEffectPreset(false), Params);
}

FACFCameraEffectHandle UACFCameraSubsystem::PlayCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, float Scale)
{
	if (!ShakeClass)
	{
		UE_LOG(LogActionCamera, Warning, TEXT("Play ACF Camera Shake: no shake class."));
		return FACFCameraEffectHandle();
	}
	TObjectPtr<UACFCameraEffectPreset>& Preset = ShakePresets.FindOrAdd(ShakeClass.Get());
	if (!Preset)
	{
		// One transient preset per shake class (created once; plays are per-instance runtime state).
		Preset = NewObject<UACFCameraEffectPreset>(this, NAME_None, RF_Transient);
		UACFCameraEffect_Shake* Layer = NewObject<UACFCameraEffect_Shake>(Preset);
		Layer->ShakeClass = ShakeClass;
		Preset->Layers.Add(Layer);
	}
	FACFCameraEffectParams Params;
	Params.Scale = Scale;
	return PlayCameraEffect(Preset, Params);
}

TArray<FACFCameraEffectInfo> UACFCameraSubsystem::GetActiveCameraEffects() const
{
	TArray<FACFCameraEffectInfo> Infos;
	if (Director)
	{
		Director->GetActiveEffects(Infos);
	}
	return Infos;
}

bool UACFCameraSubsystem::IsCameraEffectActive(FACFCameraEffectHandle Handle) const
{
	return Director && Director->IsEffectActive(Handle);
}

UACFCameraEffectPreset* UACFCameraSubsystem::GetOrCreateTransientEffectPreset(FName Key, TFunctionRef<void(UACFCameraEffectPreset*)> Build)
{
	TObjectPtr<UACFCameraEffectPreset>& Preset = TransientEffectPresets.FindOrAdd(Key);
	if (!Preset)
	{
		Preset = NewObject<UACFCameraEffectPreset>(this, Key, RF_Transient);
		Build(Preset);
	}
	return Preset;
}

// -------------------------------------------------------------------------------------------------
// Combat / constraints

void UACFCameraSubsystem::SetCombatState(const FACFCameraCombatState& State)
{
	if (Director)
	{
		Director->SetCombatState(State);
	}
}

void UACFCameraSubsystem::EnterCombat(float Intensity, AActor* FocusEnemy)
{
	FACFCameraCombatState State = GetCombatState();
	State.bInCombat = true;
	State.Intensity = FMath::Clamp(Intensity, 0.f, 1.f);
	State.FocusTarget = FACFCameraTarget();
	State.FocusTarget.Actor = FocusEnemy;
	SetCombatState(State);
}

void UACFCameraSubsystem::ExitCombat()
{
	FACFCameraCombatState State = GetCombatState();
	State.bInCombat = false;
	SetCombatState(State);
}

FACFCameraCombatState UACFCameraSubsystem::GetCombatState() const
{
	return Director ? Director->GetCombatState() : FACFCameraCombatState();
}

bool UACFCameraSubsystem::SetCameraDeoccluderEnabled(FACFCameraRequestHandle Handle, bool bEnabled)
{
	return Director && Director->SetRequestDeoccluderEnabled(Handle, bEnabled);
}

// -------------------------------------------------------------------------------------------------
// Camera sequences / finishers

FACFCameraSequenceHandle UACFCameraSubsystem::PlayCameraSequence(UACFCameraSequence* Sequence, const FACFCameraSequenceParams& Params)
{
	if (!Director)
	{
		return FACFCameraSequenceHandle();
	}
	const FACFCameraSequenceHandle Handle = Director->PlaySequence(Sequence, Params);
	if (Handle.IsValid())
	{
		EnsureOutputAttached();
	}
	FlushSequenceEvents(); // replaced playbacks
	return Handle;
}

FACFCameraSequenceHandle UACFCameraSubsystem::StartFinisherCamera(UACFCameraSequence* Sequence, AActor* Attacker, AActor* Target, float PlayRate)
{
	FACFCameraSequenceParams Params;
	Params.Attacker = Attacker;
	Params.Target = Target;
	Params.PlayRate = PlayRate;
	return PlayCameraSequence(Sequence, Params);
}

FACFCameraSequenceHandle UACFCameraSubsystem::StartFinisherCameraWithPreset(UACFCameraPreset* Preset, AActor* Attacker, AActor* Target, float Duration,
	FACFCameraBlendSettings BlendIn, FACFCameraBlendSettings ReturnBlend, int32 Priority)
{
	if (!Director)
	{
		return FACFCameraSequenceHandle();
	}
	const FACFCameraSequenceHandle Handle = Director->PlayFinisherPreset(Preset, Attacker, Target, Duration, BlendIn, ReturnBlend, Priority);
	if (Handle.IsValid())
	{
		EnsureOutputAttached();
	}
	FlushSequenceEvents();
	return Handle;
}

bool UACFCameraSubsystem::AdvanceCameraShot(FACFCameraSequenceHandle Handle)
{
	return SendCameraSequenceCommand(Handle, EACFCameraSequenceCommand::AdvanceShot, NAME_None);
}

bool UACFCameraSubsystem::JumpToCameraShot(FACFCameraSequenceHandle Handle, FName ShotName)
{
	return SendCameraSequenceCommand(Handle, EACFCameraSequenceCommand::JumpToShot, ShotName);
}

bool UACFCameraSubsystem::JumpToCameraShotIndex(FACFCameraSequenceHandle Handle, int32 ShotIndex)
{
	return Director && Director->JumpToShot(Handle, ShotIndex);
}

bool UACFCameraSubsystem::HoldCameraShot(FACFCameraSequenceHandle Handle)
{
	return SendCameraSequenceCommand(Handle, EACFCameraSequenceCommand::HoldShot, NAME_None);
}

bool UACFCameraSubsystem::ReleaseCameraShot(FACFCameraSequenceHandle Handle)
{
	return SendCameraSequenceCommand(Handle, EACFCameraSequenceCommand::ReleaseShot, NAME_None);
}

bool UACFCameraSubsystem::FinishCameraSequence(FACFCameraSequenceHandle& Handle)
{
	const bool bDone = SendCameraSequenceCommand(Handle, EACFCameraSequenceCommand::Finish, NAME_None);
	Handle.Invalidate();
	return bDone;
}

bool UACFCameraSubsystem::CancelCameraSequence(FACFCameraSequenceHandle& Handle)
{
	const bool bDone = SendCameraSequenceCommand(Handle, EACFCameraSequenceCommand::Cancel, NAME_None);
	Handle.Invalidate();
	return bDone;
}

bool UACFCameraSubsystem::SendCameraSequenceCommand(FACFCameraSequenceHandle Handle, EACFCameraSequenceCommand Command, FName ShotName)
{
	const bool bDone = Director && Director->SendSequenceCommand(Handle, Command, ShotName);
	FlushSequenceEvents();
	return bDone;
}

int32 UACFCameraSubsystem::SendCameraSequenceCommandToActor(AActor* Actor, EACFCameraSequenceCommand Command, FName ShotName)
{
	const int32 Count = Director ? Director->SendSequenceCommandToActor(Actor, Command, ShotName) : 0;
	FlushSequenceEvents();
	return Count;
}

bool UACFCameraSubsystem::SetCameraSequencePlayRate(FACFCameraSequenceHandle Handle, float PlayRate)
{
	return Director && Director->SetSequencePlayRate(Handle, PlayRate);
}

FACFCameraSequenceState UACFCameraSubsystem::GetCameraSequenceState(FACFCameraSequenceHandle Handle) const
{
	FACFCameraSequenceState State;
	if (Director)
	{
		Director->GetSequenceState(Handle, State);
	}
	return State;
}

bool UACFCameraSubsystem::IsCameraSequencePlaying(FACFCameraSequenceHandle Handle) const
{
	return Director && Director->IsSequencePlaying(Handle);
}

void UACFCameraSubsystem::FlushSequenceEvents()
{
	if (!Director || !Director->HasSequenceEvents())
	{
		return;
	}
	// Take the queue first: handlers may start / end playbacks, which queues new events for the next flush.
	Director->ConsumeSequenceEvents(SequenceEventScratch);
	for (const FACFCameraSequenceEvent& Event : SequenceEventScratch)
	{
		OnCameraSequenceEvent.Broadcast(Event);
	}
	SequenceEventScratch.Reset();
}
