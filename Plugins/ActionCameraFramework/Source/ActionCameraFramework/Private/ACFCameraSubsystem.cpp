#include "ACFCameraSubsystem.h"
#include "ACFCameraDirector.h"
#include "ACFCameraHealth.h"
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

UACFCameraSubsystem::FOnPreEvaluate UACFCameraSubsystem::OnPreEvaluate;

// -------------------------------------------------------------------------------------------------
// 外部 ViewTarget（Phase 5A）: ACF を乗せない ViewTarget の集合と、ViewTarget 単位の出力ゲート

void UACFCameraSubsystem::SetExternalViewTargets(const UObject* Source, TConstArrayView<AActor*> Targets)
{
	if (!Source)
	{
		return;
	}
	ExternalViewTargets.RemoveAll([Source](const FExternalViewTarget& Entry) { return Entry.Source.Get() == Source || !Entry.Source.IsValid() || !Entry.Target.IsValid(); });
	for (AActor* Target : Targets)
	{
		if (Target)
		{
			ExternalViewTargets.Add({ Target, Source });
		}
	}
}

void UACFCameraSubsystem::AddExternalViewTarget(AActor* ViewTarget, UObject* Source)
{
	if (!ViewTarget || !Source)
	{
		return;
	}
	const bool bExists = ExternalViewTargets.ContainsByPredicate([&](const FExternalViewTarget& Entry) { return Entry.Target.Get() == ViewTarget && Entry.Source.Get() == Source; });
	if (!bExists)
	{
		ExternalViewTargets.Add({ ViewTarget, Source });
	}
}

void UACFCameraSubsystem::RemoveExternalViewTarget(AActor* ViewTarget, UObject* Source)
{
	ExternalViewTargets.RemoveAll([&](const FExternalViewTarget& Entry) { return Entry.Target.Get() == ViewTarget && Entry.Source.Get() == Source; });
}

bool UACFCameraSubsystem::IsExternalViewTarget(const AActor* ViewTarget) const
{
	if (!ViewTarget)
	{
		return false;
	}
	// Source か Target が破棄された登録は無効（次の SetExternalViewTargets で掃除される）。
	return ExternalViewTargets.ContainsByPredicate([ViewTarget](const FExternalViewTarget& Entry) { return Entry.Target.Get() == ViewTarget && Entry.Source.IsValid(); });
}

void UACFCameraSubsystem::BroadcastPreEvaluate(float DeltaTime)
{
	if (LastPreEvaluateFrame != GFrameCounter)
	{
		LastPreEvaluateFrame = GFrameCounter;
#if ACF_WITH_PROFILER
		FACFCameraProfiler::FActiveScope ProfileActive(Director ? &Director->GetProfiler() : nullptr, GFrameCounter, DeltaTime, /*bIsEvaluation*/ false, false);
#endif
		ACF_PROFILE_SCOPE(PreEvaluate);
		OnPreEvaluate.Broadcast(*this, DeltaTime);
	}
}

bool UACFCameraSubsystem::BeginOutputForViewTarget(AActor* ViewTarget, float DeltaTime)
{
	// ゲートの判定より前に、外部の所有権管理（Sequencer モジュール）に状態を確定させる。
	BroadcastPreEvaluate(DeltaTime);
	if (IsExternalViewTarget(ViewTarget))
	{
		return false;
	}
	if (ViewTarget)
	{
		LastOutputViewTarget = ViewTarget; // 裏評価の Upstream（戻り先のゲームプレイカメラ）
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// PlayerCameraManager の互換性（Phase 6B / B-2）: Modifier の呼び出しがどの ViewTarget のものかの判定と診断

UACFCameraSubsystem::FModifierPassTarget UACFCameraSubsystem::ResolveModifierPass(const APlayerCameraManager* CameraManager, const FMinimalViewInfo& POV) const
{
	FModifierPassTarget Out;
	if (!CameraManager)
	{
		return Out; // None: 判定材料が無い。外部 ViewTarget を知りようがないので従来どおり書く。
	}
	// 1. PCM が明示した ViewTarget（最後に積まれたもの）。
	for (int32 Index = ExplicitOutputViewTargets.Num() - 1; Index >= 0; --Index)
	{
		if (ExplicitOutputViewTargets[Index].CameraManager.Get() == CameraManager)
		{
			Out.ViewTarget = ExplicitOutputViewTargets[Index].ViewTarget.Get();
			Out.Resolution = EACFViewTargetResolution::Explicit;
			Out.bWrite = !IsExternalViewTarget(Out.ViewTarget);
			Out.bPOVCopied = &POV != &CameraManager->ViewTarget.POV && &POV != &CameraManager->PendingViewTarget.POV;
			return Out;
		}
	}
	// 2. 標準 PCM: UpdateViewTarget は ViewTarget の POV を参照のまま渡す（Blend 中は切替元と行き先で 1 回ずつ）。
	if (&POV == &CameraManager->PendingViewTarget.POV)
	{
		Out.ViewTarget = CameraManager->PendingViewTarget.Target;
		Out.Resolution = EACFViewTargetResolution::POVAddress;
	}
	else if (&POV == &CameraManager->ViewTarget.POV)
	{
		Out.ViewTarget = CameraManager->ViewTarget.Target;
		Out.Resolution = EACFViewTargetResolution::POVAddress;
	}
	if (Out.Resolution == EACFViewTargetResolution::POVAddress)
	{
		Out.bWrite = !IsExternalViewTarget(Out.ViewTarget);
		return Out;
	}
	// POV はコピー（GameplayCameras の PCM、独自の PCM など）。PCM の状態から判断する。
	Out.bPOVCopied = true;
	AActor* Current = CameraManager->ViewTarget.Target;
	AActor* Pending = CameraManager->PendingViewTarget.Target;
	if (!Pending)
	{
		// 3. ViewTarget が 1 つだけ: この呼び出しはその ViewTarget のもの。
		Out.ViewTarget = Current;
		Out.Resolution = EACFViewTargetResolution::SingleViewTarget;
		Out.bWrite = !IsExternalViewTarget(Current);
		return Out;
	}
	const bool bCurrentExternal = IsExternalViewTarget(Current);
	const bool bPendingExternal = IsExternalViewTarget(Pending);
	if (bCurrentExternal == bPendingExternal)
	{
		// 4. どちらの呼び出しでも結論は同じ。裏評価の戻り先は行き先（Pending）にしておく。
		Out.ViewTarget = Pending;
		Out.Resolution = EACFViewTargetResolution::SameDecision;
		Out.bWrite = !bCurrentExternal;
		return Out;
	}
	// 5. 外部カメラとゲームプレイカメラの間のブレンドで、この呼び出しがどちらのものか分からない。
	//    推測で書くと外部カメラ（Sequencer など）を上書きしうるので、書かない。
	Out.Resolution = EACFViewTargetResolution::Unresolved;
	Out.bWrite = false;
	return Out;
}

bool UACFCameraSubsystem::BeginOutputForModifierPass(const APlayerCameraManager* CameraManager, const FMinimalViewInfo& POV, float DeltaTime)
{
	// ゲートの判定より前に、外部の所有権管理（Sequencer モジュール）に状態を確定させる。
	BroadcastPreEvaluate(DeltaTime);

	if (DiagnosedCameraManager.Get() != CameraManager)
	{
		// PCM が変わった（作り直し、別クラスへの差し替え）: 診断はその PCM ごと。
		DiagnosedCameraManager = CameraManager;
		OutputDiagnostics = FACFCameraOutputDiagnostics();
		OutputDiagnostics.CameraManagerClass = CameraManager ? CameraManager->GetClass()->GetFName() : NAME_None;
		bCompatibilityWarned = false;
	}
	const FModifierPassTarget Pass = ResolveModifierPass(CameraManager, POV);
	if (Director && CameraManager && CameraManager->ViewTarget.Target != HealthViewTarget.Get())
	{
		// B-4: the fallback pose belongs to one camera context; a new view target starts over.
		HealthViewTarget = CameraManager->ViewTarget.Target;
		Director->InvalidateLastGoodPose();
	}
	if (OutputDiagnostics.Frame != static_cast<int64>(GFrameCounter))
	{
		OutputDiagnostics.Frame = static_cast<int64>(GFrameCounter);
		OutputDiagnostics.Passes = 0;
		OutputDiagnostics.Writes = 0;
	}
	++OutputDiagnostics.Passes;
	OutputDiagnostics.LastResolution = Pass.Resolution;
	if (Pass.bPOVCopied && !OutputDiagnostics.bPOVCopied)
	{
		OutputDiagnostics.bPOVCopied = true;
		UE_LOG(LogActionCamera, Log, TEXT("ACF: %s passes a copy of the camera POV to camera modifiers; %s (README: PlayerCameraManager compatibility)."),
			*GetNameSafe(CameraManager ? CameraManager->GetClass() : nullptr),
			Pass.Resolution == EACFViewTargetResolution::Explicit ? TEXT("the camera manager reports the view target (FACFScopedOutputViewTarget)") : TEXT("ACF resolves the view target from the camera manager's state"));
	}
	if (Pass.Resolution == EACFViewTargetResolution::Unresolved)
	{
		++OutputDiagnostics.UnresolvedPasses;
		if (!bCompatibilityWarned)
		{
			bCompatibilityWarned = true; // PCM ごとに 1 回だけ
			UE_LOG(LogActionCamera, Warning, TEXT("ACF: %s passes a copy of the camera POV to camera modifiers while blending between an external view target and a gameplay view target (%s -> %s). ACF cannot tell which view target the modifier pass belongs to, so it does not write its camera during this blend. Wrap ApplyCameraModifiers in FACFScopedOutputViewTarget in the camera manager (README: PlayerCameraManager compatibility)."),
				*GetNameSafe(CameraManager->GetClass()), *GetNameSafe(CameraManager->ViewTarget.Target), *GetNameSafe(CameraManager->PendingViewTarget.Target));
		}
	}
	if (Pass.bWrite && Pass.ViewTarget)
	{
		LastOutputViewTarget = Pass.ViewTarget; // 裏評価の Upstream（戻り先のゲームプレイカメラ）
	}
	return Pass.bWrite;
}

void UACFCameraSubsystem::NoteOutputWritten()
{
	++OutputDiagnostics.Writes;
}

bool UACFCameraSubsystem::ShouldGuardModifierPass(const APlayerCameraManager* CameraManager, const FMinimalViewInfo& POV) const
{
	return ResolveModifierPass(CameraManager, POV).bWrite;
}

void UACFCameraSubsystem::PushExplicitOutputViewTarget(const APlayerCameraManager* CameraManager, AActor* ViewTarget)
{
	ExplicitOutputViewTargets.Add({ CameraManager, ViewTarget });
}

void UACFCameraSubsystem::PopExplicitOutputViewTarget()
{
	if (!ExplicitOutputViewTargets.IsEmpty())
	{
		ExplicitOutputViewTargets.Pop(EAllowShrinking::No);
	}
}

FACFScopedOutputViewTarget::FACFScopedOutputViewTarget(const APlayerCameraManager* CameraManager, AActor* ViewTarget)
{
	const APlayerController* Controller = CameraManager ? CameraManager->GetOwningPlayerController() : nullptr;
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	if (UACFCameraSubsystem* Found = LocalPlayer ? LocalPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr)
	{
		Subsystem = Found;
		Found->PushExplicitOutputViewTarget(CameraManager, ViewTarget);
	}
}

FACFScopedOutputViewTarget::~FACFScopedOutputViewTarget()
{
	if (UACFCameraSubsystem* Found = Subsystem.Get())
	{
		Found->PopExplicitOutputViewTarget();
	}
}

void UACFCameraSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Director = NewObject<UACFCameraDirector>(this, TEXT("ACFCameraDirector"));
	OutputModifierClass = GetDefault<UACFCameraSettings>()->ResolveOutputModifierClass();
	WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::HandleWorldTickStart);
	WorldPostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::HandleWorldPostActorTick);
#if !UE_BUILD_SHIPPING
	DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &ThisClass::HandleDebugDraw));
#endif
}

void UACFCameraSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
	FWorldDelegates::OnWorldPostActorTick.Remove(WorldPostActorTickHandle);
	WorldPostActorTickHandle.Reset();
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

int32 UACFCameraSubsystem::GetNumOutputSuppressions() const
{
	return Director ? Director->GetNumOutputSuppressions() : 0;
}

bool UACFCameraSubsystem::IsOutputBypassed() const
{
	return Director && Director->GetLastResult().bOutputBypassed;
}

bool UACFCameraSubsystem::BlendFromPose(const FACFCameraPose& FromPose, const FACFCameraBlendSettings& Blend)
{
	if (!Director || !Director->BlendFromPose(FromPose, Blend))
	{
		return false;
	}
	EnsureOutputAttached();
	return true;
}

int32 UACFCameraSubsystem::SetAllOrbitRotations(FRotator Rotation, bool bInstant)
{
	return Director ? Director->SetAllOrbitRotations(Rotation, bInstant) : 0;
}

void UACFCameraSubsystem::HandleWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	// このフレームに Modifier 経由で評価済みなら何もしない（通常のゲームプレイ中のコストは比較 1 回だけ）。
	// 評価されていないのは、更新された ViewTarget がすべて外部 ViewTarget だった（Sequencer の Camera Cut 中など）か、
	// デバッグ用の CameraStyle で Modifier 自体が呼ばれなかった場合。
	if (!Director || !Director->IsInfluencingOutput() || Director->GetLastEvaluatedFrame() == GFrameCounter)
	{
		return;
	}
	const APlayerCameraManager* CameraManager = GetCurrentCameraManager();
	if (!CameraManager || CameraManager->GetWorld() != World || World->IsPaused())
	{
		return; // 別のワールド、カメラ未生成、または Pause 中（カメラ更新自体が走っていない）
	}

	// Upstream（ACF が乗る UE カメラ）: 最後に ACF が映っていた ViewTarget のカメラを、PCM と同じ手順で求める。
	// これは外部カメラではなく「戻り先のゲームプレイカメラ」なので、素通し Preset や Aim Pose も戻り先基準で進む。
	FMinimalViewInfo UpstreamPOV;
	const FACFCameraPose& LastUpstream = Director->GetLastResult().UpstreamPose;
	UpstreamPOV.Location = LastUpstream.Location;
	UpstreamPOV.Rotation = LastUpstream.Rotation;
	UpstreamPOV.FOV = CameraManager->DefaultFOV;
	if (AActor* GameplayTarget = LastOutputViewTarget.Get())
	{
#if ACF_WITH_PROFILER
		FACFCameraProfiler::FActiveScope ProfileActive(&Director->GetProfiler(), GFrameCounter, DeltaSeconds, /*bIsEvaluation*/ false, true);
#endif
		ACF_PROFILE_SCOPE(BackgroundUpstream);
		GameplayTarget->CalcCamera(DeltaSeconds, UpstreamPOV);
	}
	else if (Director->GetLastResult().FrameNumber != 0)
	{
		UpstreamPOV.FOV = LastUpstream.FieldOfView;
	}
	EvaluateInternal(DeltaSeconds, UpstreamPOV, /*bOutputBypassed*/ true);
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
	return EvaluateInternal(DeltaTime, UpstreamPOV, /*bOutputBypassed*/ false);
}

const FACFCameraEvaluationResult& UACFCameraSubsystem::EvaluateInternal(float DeltaTime, const FMinimalViewInfo& UpstreamPOV, bool bOutputBypassed)
{
	check(Director);

	// 外部の所有権管理（Sequencer モジュール）に、評価の前に状態を確定させる。1 フレーム 1 回。
	BroadcastPreEvaluate(DeltaTime);

	// Stage 1: context collection.
	FACFCameraEvaluationContext Context;
	Context.DeltaTime = DeltaTime;
	Context.FrameNumber = GFrameCounter;
	Context.bOutputBypassed = bOutputBypassed;
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
	if (!Constraint.bValid || !World || !ACFCameraValidation::IsFinite(InOutLocation)) // B-4: never sweep from / to NaN (the PCM pose is left as it is)
	{
		return false;
	}
#if ACF_WITH_PROFILER
	FACFCameraProfiler::FActiveScope ProfileActive(&Director->GetProfiler(), GFrameCounter, 0.f, /*bIsEvaluation*/ false, false);
#endif
	ACF_PROFILE_SCOPE(OutputGuard);
	ACF_PROFILE_PURPOSE(OutputGuard);
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

// -------------------------------------------------------------------------------------------------
// Health (Phase 6B / B-4)

FACFCameraHealthDiagnostics UACFCameraSubsystem::GetHealthDiagnostics() const
{
	return Director ? Director->GetHealthDiagnostics() : FACFCameraHealthDiagnostics();
}

void UACFCameraSubsystem::NoteInvalidOutputUpstream()
{
	if (Director)
	{
		Director->GetHealth().Report(EACFCameraHealthIssue::InvalidOutputUpstream, NAME_None, TEXT("PCM pose under ACF (view target / earlier modifiers)"),
			TEXT("ACF pose written at full weight"));
	}
}
