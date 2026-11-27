#include "Component/CapturePresentationComponent.h"
#include "REBoarGetch.h"

#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraSubsystem.h"
#include "Effects/ACFCameraEffect.h"
#include "ACFCameraPreset.h"
#include "Nodes/ACFCameraNode_SpiralOrbit.h"
#include "Presentation/CaptureCameraPresentationData.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Boar/BoarBase.h"
#include "Boar/BoarMovementComponent.h"
#include "Core/BoarGameMode.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

namespace CapturePresentation
{
	/** 0除算を避けるための最小の長さ（秒）。 */
	constexpr float MinDuration = .01f;
	constexpr float MinAudioDuration = .001f;

	// ---- 発光・リング・データ化の見た目のカーブ（Materialの見た目に合わせて調整済みの値） ----

	/** 命中直後（フラッシュなし）とデータ化開始時の発光量。この間をSmoothStepで上げる。 */
	constexpr float GlowAmountStart = .15f;
	constexpr float GlowAmountPeak = 1.f;

	/** 捕獲リングの不透明度と、出現時に広がる倍率（開始→最大）と広がりきるまでの時間（秒）。 */
	constexpr float RingAlphaOnSpawn = .82f;
	constexpr float RingAlpha = .8f;
	constexpr float RingScaleStart = .45f;
	constexpr float RingScaleEnd = 1.25f;
	constexpr float RingExpandTime = .22f;

	/** DataMaterialのDissolveProgress: Dissolve中に0→0.84、転送完了でさらに+0.17（1を少し超えて完全に消える）。 */
	constexpr float DissolveProgressAtDissolveEnd = .84f;
	constexpr float DissolveProgressFinishAdd = .17f;

	/** DataMaterialの発光量: Dissolve開始時に1＋この値、Dissolve終了時に1へ下げる。 */
	constexpr float DataGlowExtra = .45f;

	/** Cage受信リングの不透明度・Scale（VFXScaleに対する倍率）・広がり方。 */
	constexpr float ReceptionAlpha = .9f;
	constexpr float ReceptionSpawnScale = .65f;
	constexpr float ReceptionScaleStart = .3f;
	constexpr float ReceptionScaleEnd = .85f;

	/** FOV Punch Inで設定できる視野角の範囲（度）。 */
	constexpr float MinFieldOfView = 5.f;
	constexpr float MaxFieldOfView = 170.f;

	/** 減速倍率の下限。0にするとアニメーション・移動が完全に止まって復帰計算が崩れるため。 */
	constexpr float MinSlowRate = .001f;

	/** 複数体捕獲の強調が、最初の捕獲からこの秒数以内（同じフレームの複数Hit）なら通常のEffectを即座に置き換える。 */
	constexpr float SameFrameMultiCaptureWindow = .05f;

	/** 収容でこれ以上（cm）動いたBoarだけをCage受信演出の対象にする。 */
	constexpr float HousedMoveTolerance = 1.f;

	// ---- テレポートVFXに渡すBoarの大きさ ----

	/** MeshのBoundsが取れないときの既定値と、小さすぎるBoundsに対する下限（cm）。 */
	constexpr float TeleportDefaultRadius = 60.f;
	constexpr float TeleportDefaultHeight = 100.f;
	constexpr float TeleportMinRadius = 20.f;
	constexpr float TeleportMinHeight = 30.f;

	/** User.CommitTime の下限（秒）。 */
	constexpr float TeleportMinCommitTime = .05f;

	// ---- Orbitの事前確認 ----

	/** 軌道上を調べる最少の点数と、Sweepする球の半径（cm）。 */
	constexpr int32 MinOrbitProbeSamples = 4;
	constexpr float OrbitProbeRadius = 12.f;

	/** デバッグ表示で計画した螺旋を描く分割数。 */
	constexpr int32 DebugSpiralSegments = 32;

	// ---- Audio V2（Spiral音のエンベロープ） ----

	/** 命中音のピッチの揺らぎ幅（毎回少しだけ変えて単調にしない）。 */
	constexpr float ImpactPitchMin = .97f;
	constexpr float ImpactPitchMax = 1.03f;

	/** Spiral音の開始時の音量・ピッチ・Low Pass周波数（Hz）。 */
	constexpr float SpiralBaseVolume = .12f;
	constexpr float SpiralBasePitch = .78f;
	constexpr float SpiralBaseLowPass = 450.f;

	/** 各段階（包み込み Envelop / 上昇 Rise / 収束 Collapse）がSpiral音へ足す量。 */
	constexpr float SpiralPitchEnvelop = .12f;
	constexpr float SpiralPitchRise = .53f;
	constexpr float SpiralPitchCollapse = .13f;
	constexpr float SpiralLowPassEnvelop = 350.f;
	constexpr float SpiralLowPassRise = 2600.f;
	constexpr float SpiralLowPassCollapse = 650.f;
	constexpr float SpiralVolumeEnvelop = .23f;
	constexpr float SpiralVolumeRise = .47f;

	// ---- 既定のCamera Shake（BP_CaptureSuccessCameraShakeのClass Defaultsで上書きできる） ----

	constexpr float ShakeDuration = .25f;
	constexpr float ShakeBlendInTime = .02f;
	constexpr float ShakeBlendOutTime = .09f;
	constexpr float ShakePitchAmplitude = .3f;
	constexpr float ShakeYawAmplitude = .17f;
	constexpr float ShakeRollAmplitude = .12f;
	constexpr float ShakeFrequency = 24.f;
}

static TAutoConsoleVariable<int32> CVarCaptureAudioLegacy(
	TEXT("Boar.Capture.AudioLegacy"),
	0,
	TEXT("捕獲音のA/B比較: 0 = V2の音が設定されていればV2を使う、1 = 従来の音を使う。次の捕獲から反映されます。"));

UCaptureSuccessCameraShake::UCaptureSuccessCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	using namespace CapturePresentation;

	bSingleInstance = true;

	// 位置は揺らさず、回転だけを短く細かく揺らす。
	auto* Pattern = CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("CaptureShake"));
	Pattern->Duration = ShakeDuration;
	Pattern->BlendInTime = ShakeBlendInTime;
	Pattern->BlendOutTime = ShakeBlendOutTime;
	Pattern->LocationAmplitudeMultiplier = 0.f;
	Pattern->Pitch.Amplitude = ShakePitchAmplitude;
	Pattern->Yaw.Amplitude = ShakeYawAmplitude;
	Pattern->Roll.Amplitude = ShakeRollAmplitude;
	Pattern->Pitch.Frequency = Pattern->Yaw.Frequency = Pattern->Roll.Frequency = ShakeFrequency;
	Pattern->FOV.Amplitude = 0.f;

	SetRootShakePattern(Pattern);
}

UCapturePresentationComponent::UCapturePresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UCapturePresentationComponent::GetTotalDuration() const
{
	return HitStopDuration + SlowDuration + CaptureDissolveDuration + TransferFinishDuration;
}

void UCapturePresentationComponent::BeginPlay()
{
	Super::BeginPlay();

	LastRealTime = GetWorld()->GetRealTimeSeconds();
	TickHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::OnWorldTick);

	ReportMissingVisualAssets();
}

// 必須の演出アセットが未設定なら開始時に1回だけErrorを出す。欠けた演出は個別に省かれ、捕獲処理は継続する。
// 演出全体を意図的にOFF（bEnabled=false）にしている場合は報告しない。効果音は任意（未設定なら無音）。
void UCapturePresentationComponent::ReportMissingVisualAssets() const
{
	auto* PC = Cast<APlayerController>(GetOwner());
	if (!bEnabled || !PC || !PC->IsLocalController())
		return;

	TArray<FString> Missing;

	if (!GlowMaterial)
		Missing.Add(TEXT("GlowMaterial（Boarの発光）"));

	if (!DataMaterial)
		Missing.Add(TEXT("DataMaterial（データ化）"));

	if (!CaptureVFX)
		Missing.Add(TEXT("CaptureVFX（捕獲リング）"));

	if (!CageReceptionVFX)
		Missing.Add(TEXT("CageReceptionVFX（Cage受信）"));

	if (!DataFragmentVFX)
		Missing.Add(TEXT("DataFragmentVFX（データ粒子）"));

	if (!ImpactVFX)
		Missing.Add(TEXT("ImpactVFX（命中インパクト）"));

	if (!TeleportVFX)
		Missing.Add(TEXT("TeleportVFX（青白いテレポート）"));

	if (bUseFocusCamera && !FocusSequence && !CameraPresentation)
		Missing.Add(TEXT("FocusSequence（ACFの捕獲Focusカメラ）"));

	if (!CameraShake || CameraShake->HasAnyClassFlags(CLASS_Abstract))
		Missing.Add(TEXT("CameraShake（BP_CaptureSuccessCameraShake）"));

	if (!Missing.IsEmpty())
	{
		UE_LOG(LogREBoarGetch, Error, TEXT("[Capture] %s.%s: %s が未設定です。該当する演出だけを省きます（捕獲処理は継続します）。"),
			*GetNameSafe(GetOwner()), *GetName(), *FString::Join(Missing, TEXT("、")));
	}
}

void UCapturePresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	FWorldDelegates::OnWorldTickStart.Remove(TickHandle);
	CancelPresentation(false);
	Super::EndPlay(Reason);
}

bool UCapturePresentationComponent::PresentCapturedBoar(ABoarBase* Boar, bool bSharedFeedback)
{
	using namespace CapturePresentation;

	auto* PC = Cast<APlayerController>(GetOwner());
	if (!bEnabled || !PC || !PC->IsLocalController() || !IsValid(Boar) || !Boar->IsCaptured())
		return false;

	if (Targets.ContainsByPredicate([Boar](const FCaptureVisualTarget& T) { return T.Boar == Boar; }))
		return true;

	if (Targets.IsEmpty())
	{
		LastRealTime = GetWorld()->GetRealTimeSeconds();
	}

	// ---- 対象1体分の見た目（発光・インパクト・捕獲リング） ----
	FCaptureVisualTarget T;
	T.Boar = Boar;
	T.Mesh = Boar->GetMesh();
	T.CaptureLocation = Boar->GetActorLocation();

	if (T.Mesh.IsValid())
	{
		T.bOriginalVisible = T.Mesh->IsVisible();
	}

	if (T.Mesh.IsValid() && GlowMaterial)
	{
		const FBoxSphereBounds Bounds = T.Mesh->Bounds;
		T.CaptureMinZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
		T.CaptureHeight = FMath::Max(1.f, Bounds.BoxExtent.Z * 2.f);
		T.OriginalOverlay = T.Mesh->GetOverlayMaterial();
		T.Glow = UMaterialInstanceDynamic::Create(GlowMaterial, this);

		// 命中の瞬間は白いフラッシュから始め、UpdateTargetで通常の発光へ戻す。
		const bool bFlash = HitFlashDuration > 0.f;
		T.Glow->SetScalarParameterValue(TEXT("GlowAmount"), bFlash ? HitFlashGlowAmount : GlowAmountStart);
		T.Glow->SetVectorParameterValue(TEXT("GlowColor"), bFlash ? HitFlashColor : GlowStartColor);
		T.Mesh->SetOverlayMaterial(T.Glow);
	}

	// 命中インパクト（Boarごと）。捕獲の確定とは無関係の使い捨て演出。
	SpawnImpact(T);

	if (CaptureVFX)
	{
		T.VFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CaptureVFX,
			Boar->GetActorLocation() - FVector(0, 0, CaptureRingHeightOffset),
			FRotator::ZeroRotator, FVector(VFXScale), false, false);

		if (T.VFX)
		{
			T.VFX->SetVariableLinearColor(TEXT("User.Color"),
				FLinearColor(CaptureColor.R, CaptureColor.G, CaptureColor.B, RingAlphaOnSpawn));
			T.VFX->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
			T.VFX->SetCanRenderWhileSeeking(true);
			T.VFX->Activate(true);
		}
	}

	Targets.Add(T);
	Boar->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnTargetDestroyed);

	// 1回のNet使用で捕獲した数。NetGadgetは使用ごとの最初の捕獲だけbSharedFeedback=trueで呼ぶ。
	if (bSharedFeedback)
	{
		SameUseCaptureCount = 1;
		bMultiFeedbackPlayed = false;
	}
	else
	{
		++SameUseCaptureCount;
	}

	if (bSharedFeedback)
	{
		BeginCaptureAudio(Boar);
	}
	else if (!CaptureAudioTargets.IsEmpty())
	{
		CaptureAudioTargets.Add(Boar);
	}

	if (bSharedFeedback && !bSharedActive)
	{
		// ---- 共有演出の開始（Net 1回につき1回） ----
		bSharedActive = true;
		SharedAge = 0.f;
		bConfirmed = false;
		bTransferSoundPlayed = false;

		// 捕獲Orbitカメラ（ACF Sequence + Spiral Orbit）。ACFの通常カメラが出ているときだけ、Net 1回につき1回。
		// 注視点は命中時のBoundsから決めてFocus Proxyに保持し、Boarが消えてもOrbitの中心は動かない。
		// 前のNetのOrbitがまだ回っている間は新しいOrbitを始めない（カメラが途中で跳ばない・Requestが重複しない）。
		FocusPoints.Reset();
		const FVector Focus = ComputeFocusPoint(Boar);
		FocusPoints.Add(Focus);

		UACFCameraSubsystem* Cameras = GetACFSubsystem();
		const bool bOrbitRunning = ActiveFocus.IsValid() && Cameras && Cameras->IsCameraSequencePlaying(ActiveFocus);
		UACFCameraSequence* OrbitSequence = bUseFocusCamera && !bOrbitRunning ? ResolveFocusSequence() : nullptr;

		if (OrbitSequence && IsOrbitFeasible(OrbitSequence, Focus, Boar))
		{
			if (!PlayFocusCamera(OrbitSequence, PlaceFocusProxy(Focus)))
			{
				ReleaseFocusCamera(true);
			}
		}

		// 捕獲時のカメラの揺れは1つにまとめる。
		// - Orbitを再生したとき: CS_CaptureOrbitのShot Effects（DA_CaptureOrbitShake）がOrbitと同時に揺らすので、ここでは何も重ねない。
		// - Orbitを省略したとき（ACFあり）: ACFのCamera Effect＋UE Camera Shake。
		// - ACFが無いとき: FollowCameraのFOV Punch In＋UE Camera Shake（従来どおり）。復元はRestoreSharedState()が担当する。
		const bool bOrbitStarted = ActiveFocus.IsValid() && Cameras && Cameras->IsCameraSequencePlaying(ActiveFocus);
		bUsingACFEffect = bOrbitStarted || PlayCameraEffect(CameraEffect, T.CaptureLocation, false);

		Camera = !bUsingACFEffect && PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UCameraComponent>() : nullptr;
		if (Camera.IsValid())
		{
			OriginalFOV = AppliedFOV = Camera->FieldOfView;
			bOwnCamera = true;
		}

		// World全体の時間は変えず、捕獲地点の周囲にいる未捕獲Boarだけを減速する。
		SlowedBoars.Reset();
		CollectLocalSlowTargets(T.CaptureLocation);
		ApplyLocalSlow(HitStopDuration > 0);

		if (!bOrbitStarted && PC->PlayerCameraManager && CameraShake && !CameraShake->HasAnyClassFlags(CLASS_Abstract))
		{
			ActiveShake = PC->PlayerCameraManager->StartCameraShake(CameraShake);
		}

		if (!bCaptureAudioV2)
		{
			PlaySound(ImpactSound);
		}
	}
	else if (bSharedActive && SharedAge < HitStopDuration + SlowDuration)
	{
		// 同じ演出中に別の地点で捕獲した場合も、その周囲を同じ減速へ加える（共有演出は開始し直さない）。
		CollectLocalSlowTargets(T.CaptureLocation);
		ApplyLocalSlow(SharedAge < HitStopDuration);

		// 同じNetで捕獲したBoarが増えたら、Orbitの中心をBoar群の中心へ移す（Orbitは1つのまま、Spiral Orbitが滑らかに追う）。
		if (bFocusOnGroupCenter && IsValid(FocusProxy) && !FocusPoints.IsEmpty())
		{
			FocusPoints.Add(ComputeFocusPoint(Boar));

			FVector Sum = FVector::ZeroVector;

			for (const FVector& P : FocusPoints)
			{
				Sum += P;
			}

			FocusProxy->SetActorLocation(Sum / FocusPoints.Num());
		}
	}

	// 複数体の強調はNet 1回につき1回だけ。個々のBoarの見た目（発光・インパクト・データ化）は体ごとに出す。
	if (bSharedActive && !bMultiFeedbackPlayed && SameUseCaptureCount >= MultiCaptureThreshold)
	{
		PlayMultiCaptureFeedback(T.CaptureLocation, Boar);
	}

	return true;
}

void UCapturePresentationComponent::PlaySound(USoundBase* Sound, float Pitch)
{
	if (!Sound || !GetWorld() || !GetWorld()->GetGameInstance())
		return;

	if (auto* Audio = GetWorld()->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>())
	{
		Audio->PlaySoundEffect2D(this, Sound, 1.f, Pitch);
	}
}

UAudioComponent* UCapturePresentationComponent::SpawnCaptureAudio(USoundBase* Sound, float Volume, float Pitch)
{
	if (UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (auto* Audio = Instance->GetSubsystem<UBoarAudioManagerSubsystem>())
		{
			return Audio->SpawnSoundEffect2D(this, Sound, Volume, Pitch);
		}
	}

	return nullptr;
}

void UCapturePresentationComponent::BeginCaptureAudio(ABoarBase* Boar)
{
	using namespace CapturePresentation;

	StopCaptureAudio();

	// V2の4音がすべて設定されていて、CVarで従来音に切り替えていないときだけV2を使う。
	bCaptureAudioV2 = !CVarCaptureAudioLegacy.GetValueOnGameThread()
		&& CaptureImpactV2 && CaptureSpiralV2 && CaptureConfirmV2 && CaptureMultiV2;

	if (!bCaptureAudioV2)
		return;

	CaptureAudioTargets.Add(Boar);
	CaptureAudioAge = 0.f;
	CaptureImpactVoice = SpawnCaptureAudio(CaptureImpactV2, 1.f, FMath::FRandRange(ImpactPitchMin, ImpactPitchMax));

	// 軌道が省略されても、同じ実時間で進む転送フェーズに追従する。
	CaptureSpiralVoice = SpawnCaptureAudio(CaptureSpiralV2, SpiralBaseVolume, SpiralBasePitch);
	if (UAudioComponent* Voice = CaptureSpiralVoice.Get())
	{
		Voice->SetLowPassFilterEnabled(true);
		Voice->SetLowPassFilterFrequency(SpiralBaseLowPass);
	}
}

void UCapturePresentationComponent::UpdateCaptureAudio(float RealDelta)
{
	using namespace CapturePresentation;

	CaptureAudioAge += RealDelta;

	UAudioComponent* Voice = CaptureSpiralVoice.Get();
	if (!Voice)
		return;

	const float TeleportStart = GetTeleportStart();
	const float CollapseStart = HitStopDuration + SlowDuration + CaptureDissolveDuration;
	const float End = GetTotalDuration();

	// 3つの段階をそれぞれ0→1の進行度にする。
	// Envelop: 命中〜テレポート開始、Rise: テレポート開始〜データ化終了、Collapse: データ化終了〜捕獲確定。
	const float Envelop = FMath::Clamp(CaptureAudioAge / FMath::Max(MinAudioDuration, TeleportStart), 0.f, 1.f);
	const float Rise = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(
		(CaptureAudioAge - TeleportStart) / FMath::Max(MinAudioDuration, CollapseStart - TeleportStart), 0.f, 1.f));
	const float Collapse = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(
		(CaptureAudioAge - CollapseStart) / FMath::Max(MinAudioDuration, End - CollapseStart), 0.f, 1.f));

	Voice->SetPitchMultiplier(SpiralBasePitch
		+ SpiralPitchEnvelop * Envelop
		+ SpiralPitchRise * Rise
		+ SpiralPitchCollapse * Collapse);

	Voice->SetLowPassFilterFrequency(SpiralBaseLowPass
		+ SpiralLowPassEnvelop * Envelop
		+ SpiralLowPassRise * Rise
		- SpiralLowPassCollapse * Collapse);

	if (auto* Audio = GetWorld()->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>())
	{
		const float Envelope = (SpiralBaseVolume + SpiralVolumeEnvelop * Envelop + SpiralVolumeRise * Rise) * (1.f - Collapse);
		Voice->SetVolumeMultiplier(Audio->GetMasterVolume() * Audio->GetSoundEffectVolume() * Envelope);
	}

	if (CaptureAudioAge >= End)
	{
		Voice->Stop();
		CaptureSpiralVoice.Reset();
	}
}

void UCapturePresentationComponent::ConfirmCaptureAudio(const TArray<ABoarBase*>& Confirmed)
{
	if (CaptureAudioTargets.IsEmpty())
		return;

	const bool bAnyAudioTargetConfirmed = Confirmed.ContainsByPredicate([this](ABoarBase* Boar)
	{
		return CaptureAudioTargets.Contains(TWeakObjectPtr<ABoarBase>(Boar));
	});

	if (!bAnyAudioTargetConfirmed)
		return;

	StopCaptureAudio();

	USoundBase* ConfirmSound = SameUseCaptureCount >= MultiCaptureThreshold ? CaptureMultiV2.Get() : CaptureConfirmV2.Get();
	CaptureConfirmVoice = SpawnCaptureAudio(ConfirmSound, 1.f);
}

void UCapturePresentationComponent::StopCaptureAudio(bool bKeepConfirmedTail)
{
	const auto Stop = [](TWeakObjectPtr<UAudioComponent>& Voice)
	{
		if (UAudioComponent* Component = Voice.Get())
		{
			Component->Stop();
		}

		Voice.Reset();
	};

	Stop(CaptureImpactVoice);
	Stop(CaptureSpiralVoice);

	if (!bKeepConfirmedTail)
	{
		Stop(CaptureConfirmVoice);
	}

	CaptureAudioTargets.Reset();
}

void UCapturePresentationComponent::SpawnImpact(const FCaptureVisualTarget& T)
{
	if (!ImpactVFX || !T.Boar.IsValid())
		return;

	FVector Location = T.CaptureLocation;

	if (T.Mesh.IsValid())
	{
		// 足元(0)〜頭頂(1)のうちImpactHeightRatioの高さに出す。
		const FBoxSphereBounds Bounds = T.Mesh->Bounds;
		Location = Bounds.Origin;
		Location.Z = Bounds.Origin.Z - Bounds.BoxExtent.Z + Bounds.BoxExtent.Z * 2.f * ImpactHeightRatio;
	}

	// 自動で破棄される使い捨て。粒子の動きはNiagara側で決め、C++は位置・色・Scaleだけを渡す。
	if (UNiagaraComponent* VFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, ImpactVFX, Location, FRotator::ZeroRotator, FVector(ImpactVFXScale), true, false))
	{
		VFX->SetVariableLinearColor(TEXT("User.Color"), ImpactColor);
		VFX->Activate(true);
	}
}

UACFCameraSubsystem* UCapturePresentationComponent::GetACFSubsystem() const
{
	const auto* PC = Cast<APlayerController>(GetOwner());
	return PC && PC->IsLocalController() ? UACFCameraBlueprintLibrary::GetCameraSubsystem(PC) : nullptr;
}

bool UCapturePresentationComponent::PlayCameraEffect(UACFCameraEffectPreset* Preset, const FVector& Source,
	bool bReplaceImmediately)
{
	UACFCameraSubsystem* Cameras = GetACFSubsystem();
	if (!Preset || !Cameras || !Cameras->CanPlayCameraEffects())
		return false;

	ReleaseCameraEffect(bReplaceImmediately);

	FACFCameraEffectParams Params;
	Params.bUseSourceLocation = true;
	Params.SourceLocation = Source;
	Params.Instigator = this; // このComponentが破棄されたらACF側でも止まる。

	ActiveCameraEffect = Cameras->PlayCameraEffect(Preset, Params);
	return ActiveCameraEffect.IsValid();
}

// 自分が再生したEffectだけを止める（Stop All は使わない）。何度呼んでも安全。
void UCapturePresentationComponent::ReleaseCameraEffect(bool bImmediate)
{
	if (!ActiveCameraEffect.IsValid())
		return;

	if (UACFCameraSubsystem* Cameras = GetACFSubsystem())
	{
		Cameras->StopCameraEffect(ActiveCameraEffect, bImmediate);
	}

	ActiveCameraEffect.Invalidate();
}

void UCapturePresentationComponent::PlayMultiCaptureFeedback(const FVector& Source, ABoarBase* Boar)
{
	bMultiFeedbackPlayed = true;

	if (!bCaptureAudioV2)
	{
		PlaySound(MultiCaptureSound);
	}

	// 通常のEffectと重ねず入れ替える。同じフレームの複数Hitなら通常分は即座に消す。
	// Orbitカメラは入れ替えない（途中で入れ替えると視点が跳ぶ）。複数体は注視点をBoar群の中心へ移す（PresentCapturedBoar）。
	if (bUsingACFEffect && MultiCaptureCameraEffect && !IsValid(FocusProxy))
	{
		PlayCameraEffect(MultiCaptureCameraEffect, Source, SharedAge < CapturePresentation::SameFrameMultiCaptureWindow);
	}
}

ACaptureFocusProxy::ACaptureFocusProxy()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Focus"));
}

FVector UCapturePresentationComponent::ComputeFocusPoint(const ABoarBase* Boar) const
{
	if (!IsValid(Boar))
		return FVector::ZeroVector;

	if (const USkeletalMeshComponent* Mesh = Boar->GetMesh())
	{
		// 足元(0)〜頭頂(1)のうちFocusHeightRatioの高さ。
		const FBoxSphereBounds B = Mesh->Bounds;
		return FVector(B.Origin.X, B.Origin.Y, B.Origin.Z - B.BoxExtent.Z + 2.f * B.BoxExtent.Z * FocusHeightRatio);
	}

	return Boar->GetActorLocation();
}

AActor* UCapturePresentationComponent::PlaceFocusProxy(const FVector& FocusPoint)
{
	if (IsValid(FocusProxy))
	{
		FocusProxy->SetActorLocation(FocusPoint);
		return FocusProxy;
	}

	FActorSpawnParameters Spawn;
	Spawn.Owner = GetOwner();
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.ObjectFlags |= RF_Transient;

	FocusProxy = GetWorld() ? GetWorld()->SpawnActor<ACaptureFocusProxy>(FocusPoint, FRotator::ZeroRotator, Spawn) : nullptr;
	return FocusProxy;
}

const UACFCameraNode_SpiralOrbit* UCapturePresentationComponent::FindSpiralOrbit(const UACFCameraSequence* Sequence) const
{
	if (!Sequence || Sequence->Shots.IsEmpty() || !Sequence->Shots[0].Preset)
		return nullptr;

	for (const UACFCameraNode* Node : Sequence->Shots[0].Preset->Nodes)
	{
		if (const auto* Orbit = Cast<UACFCameraNode_SpiralOrbit>(Node); Orbit && Orbit->bEnabled)
		{
			return Orbit;
		}
	}

	return nullptr;
}

float UCapturePresentationComponent::ComputeOrbitStartYaw(const UACFCameraNode_SpiralOrbit& Orbit, const FVector& Focus) const
{
	// UACFCameraNode_SpiralOrbitの開始角と同じ規則（デバッグ表示と事前確認を実際の軌道と一致させる）。
	float Yaw = Orbit.StartAngleOffset;
	const auto* PC = Cast<APlayerController>(GetOwner());

	if (Orbit.StartMode == EACFCameraSpiralStart::ControlRotation && PC)
	{
		// カメラの真後ろ（Control RotationのYaw＋180度）から回り始める。
		Yaw += PC->GetControlRotation().Yaw + 180.f;
	}
	else if (Orbit.StartMode == EACFCameraSpiralStart::FollowTarget && PC && PC->GetPawn())
	{
		const FVector To = PC->GetPawn()->GetActorLocation() - Focus;
		Yaw += FVector(To.X, To.Y, 0.0).IsNearlyZero() ? 0.f : static_cast<float>(To.Rotation().Yaw);
	}

	return Yaw;
}

bool UCapturePresentationComponent::IsOrbitFeasible(const UACFCameraSequence* Sequence, const FVector& Focus, const ABoarBase* Boar) const
{
	using namespace CapturePresentation;

	const UACFCameraNode_SpiralOrbit* Orbit = FindSpiralOrbit(Sequence);
	UWorld* World = GetWorld();
	if (!Orbit || !World)
		return Sequence != nullptr; // Orbit以外のShotはACF自身のFallback（Skip If Infeasible）に任せる。

	const auto* PC = Cast<APlayerController>(GetOwner());
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CaptureOrbitProbe), false);
	Query.AddIgnoredActor(Boar);

	if (PC && PC->GetPawn())
	{
		Query.AddIgnoredActor(PC->GetPawn());
	}

	if (IsValid(FocusProxy))
	{
		Query.AddIgnoredActor(FocusProxy);
	}

	// 螺旋の軌道を等間隔（各区間の中央）で調べ、注視点からカメラ位置までの途中で何かに当たる点を数える。
	const float StartYaw = ComputeOrbitStartYaw(*Orbit, Focus);
	const int32 Samples = FMath::Max(MinOrbitProbeSamples, OrbitProbeSamples);
	int32 Blocked = 0;

	for (int32 I = 0; I < Samples; ++I)
	{
		const float SampleTime = Orbit->Duration * (I + .5f) / Samples;
		const FACFCameraSpiralSample S = Orbit->SampleSpiral(Focus, StartYaw, SampleTime);
		const float Distance = FVector::Dist(S.LookAt, S.Location);

		FHitResult Hit;
		const bool bHit = World->SweepSingleByChannel(Hit, S.LookAt, S.Location, FQuat::Identity, ECC_Camera,
			FCollisionShape::MakeSphere(OrbitProbeRadius), Query);
		if (bHit && Hit.Distance < Distance * OrbitMinClearanceRatio)
		{
			++Blocked;
		}
	}

	const float Fraction = static_cast<float>(Blocked) / Samples;
	const bool bFeasible = Fraction <= OrbitMaxBlockedFraction;

	UE_LOG(LogREBoarGetch, Log, TEXT("[CaptureOrbit] 事前確認: %d/%d 点が塞がり（%.0f%%、上限 %.0f%%）-> %s"),
		Blocked, Samples, Fraction * 100.f, OrbitMaxBlockedFraction * 100.f,
		bFeasible ? TEXT("Orbitを再生") : TEXT("省略（通常カメラ）"));

	return bFeasible;
}

UACFCameraSequence* UCapturePresentationComponent::ResolveFocusSequence()
{
	// 1つのDataAsset（回転＋揺れ＋Collision＋時間）から毎回組み立てる。安価（Preset・Effect・Sequenceの3オブジェクト）で、
	// PIE中にDataAssetを変更しても次の捕獲から反映される。
	if (CameraPresentation)
	{
		if (UACFCameraSequence* Built = CameraPresentation->BuildSequence(this))
		{
			return Built;
		}
	}

	return FocusSequence;
}

bool UCapturePresentationComponent::PlayFocusCamera(UACFCameraSequence* Sequence, AActor* Target)
{
	auto* PC = Cast<APlayerController>(GetOwner());
	UACFCameraSubsystem* Cameras = GetACFSubsystem();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	// ACFの通常カメラが出ていないとき（ACF無効・Sequencer・勝利カメラなど）は再生しない。従来のFallbackに任せる。
	if (!Sequence || !Cameras || !Pawn || !IsValid(Target) || !Cameras->CanPlayCameraEffects())
		return false;

	FACFCameraSequenceParams Params;

	// Attacker=Player（Spiral OrbitのStart Mode=Follow Target用）、Target=Focus Proxy（Orbitの中心。Boarが消えても残る）。
	Params.Attacker = Pawn;
	Params.Target = Target;

	// このComponentが破棄されたらACF側でも終わる。他の演出のSequenceは止めない。
	Params.Instigator = this;
	Params.bStopOtherSequences = false;

	FACFCameraSequenceHandle Previous = ActiveFocus;
	const FACFCameraSequenceHandle Started = Cameras->PlayCameraSequence(Sequence, Params);
	if (!Started.IsValid())
		return false;

	ActiveFocus = Started;
	ActiveFocusSequence = Sequence;

	// 新しい再生のRequestが出た後で前の再生を外す（同じPriorityでは後から出した要求が勝つので画面は跳ばない）。
	if (Previous.IsValid() && Previous != Started && Cameras->IsCameraSequencePlaying(Previous))
	{
		Cameras->CancelCameraSequence(Previous);
	}

	// デバッグ用の記録（実際のカメラ位置が注視点の周りを何度回ったかを測る）。
	OrbitFocusAtStart = Target->GetActorLocation();
	const UACFCameraNode_SpiralOrbit* Orbit = FindSpiralOrbit(Sequence);
	OrbitStartYaw = Orbit ? ComputeOrbitStartYaw(*Orbit, OrbitFocusAtStart) : 0.f;
	OrbitRealAge = OrbitActualDegrees = 0.f;
	OrbitMinHeight = TNumericLimits<float>::Max();
	OrbitMaxHeight = TNumericLimits<float>::Lowest();
	bOrbitHasPrevYaw = false;
	OrbitTrail.Reset();

	StartOrbitSound(Orbit);

	return true;
}

void UCapturePresentationComponent::StartOrbitSound(const UACFCameraNode_SpiralOrbit* Orbit)
{
	StopOrbitSound(false);

	if (!OrbitSound)
		return;

	CaptureOrbitVoice = SpawnCaptureAudio(OrbitSound, OrbitSoundVolume);

	// フェードアウトはOrbitの終わりで無音になるように始める。OrbitのPresetが読めないときはフェードせず、Orbitの終了で止める。
	const float OrbitDuration = Orbit ? Orbit->Duration : 0.f;
	OrbitSoundFadeStart = OrbitDuration > 0.f ? FMath::Max(0.f, OrbitDuration - OrbitSoundFadeOutTime) : TNumericLimits<float>::Max();
	bOrbitSoundFading = false;
}

void UCapturePresentationComponent::UpdateOrbitSound()
{
	UAudioComponent* Voice = CaptureOrbitVoice.Get();
	if (!Voice || bOrbitSoundFading || OrbitRealAge < OrbitSoundFadeStart)
		return;

	bOrbitSoundFading = true;
	if (OrbitSoundFadeOutTime > 0.f)
	{
		Voice->FadeOut(OrbitSoundFadeOutTime, 0.f);
	}
	else
	{
		Voice->Stop();
	}
}

void UCapturePresentationComponent::StopOrbitSound(bool bFade)
{
	if (UAudioComponent* Voice = CaptureOrbitVoice.Get())
	{
		// 自然終了ではフェード中ならそのまま消えるのを待つ。中断はすぐに止める。
		if (!bFade)
		{
			Voice->Stop();
		}
		else if (!bOrbitSoundFading)
		{
			Voice->FadeOut(FMath::Max(KINDA_SMALL_NUMBER, OrbitSoundFadeOutTime), 0.f);
		}
	}

	CaptureOrbitVoice.Reset();
	bOrbitSoundFading = false;
}

void UCapturePresentationComponent::ReleaseFocusCamera(bool bCancel)
{
	if (ActiveFocus.IsValid())
	{
		if (UACFCameraSubsystem* Cameras = GetACFSubsystem(); Cameras && Cameras->IsCameraSequencePlaying(ActiveFocus))
		{
			if (bCancel)
			{
				Cameras->CancelCameraSequence(ActiveFocus);
			}
			else
			{
				Cameras->FinishCameraSequence(ActiveFocus);
			}
		}

		ActiveFocus.Invalidate();
	}

	StopOrbitSound(!bCancel);

	// Return Blend中もACFはRequestの最後の注視点を保持するので、Proxyは再生の終了と同時に破棄してよい。
	if (IsValid(FocusProxy))
	{
		FocusProxy->Destroy();
	}

	FocusProxy = nullptr;
	FocusPoints.Reset();
}

namespace CaptureOrbitDebug
{
	static int32 Enabled = 0;
	static FAutoConsoleVariableRef CVar(
		TEXT("Boar.Capture.OrbitDebug"),
		Enabled,
		TEXT("1 = 捕獲Orbitを描画（計画した螺旋・注視点・実カメラの軌跡）し、進行度・角度・半径・高さ・Handle・段階を画面に表示します。"));
}

void UCapturePresentationComponent::UpdateFocusCamera(float RealDelta)
{
	if (!ActiveFocus.IsValid())
		return;

	UACFCameraSubsystem* Cameras = GetACFSubsystem();
	auto* PC = Cast<APlayerController>(GetOwner());

	if (!Cameras || !Cameras->IsCameraSequencePlaying(ActiveFocus))
	{
		// Shotが終わりReturn BlendでTPSへ戻り始めた（またはACF側で終了した）。実カメラの計測結果を1行残す。
		const float MinHeight = OrbitMinHeight == TNumericLimits<float>::Max() ? 0.f : OrbitMinHeight;
		const float MaxHeight = OrbitMaxHeight == TNumericLimits<float>::Lowest() ? 0.f : OrbitMaxHeight;
		UE_LOG(LogREBoarGetch, Log, TEXT("[CaptureOrbit] 終了: カメラは注視点の周りを %.0f 度移動、高さ %.0f -> %.0f cm、%.2f 秒（実時間）"),
			OrbitActualDegrees, MinHeight, MaxHeight, OrbitRealAge);

		ReleaseFocusCamera(false);
		return;
	}

	OrbitRealAge += RealDelta;
	UpdateOrbitSound();
	const FVector Focus = IsValid(FocusProxy) ? FocusProxy->GetActorLocation() : OrbitFocusAtStart;

	// 実際に描画されたカメラ位置を注視点基準の円柱座標で測る（Yawは巻き戻して累積する）。
	if (PC && PC->PlayerCameraManager)
	{
		const FVector Rel = PC->PlayerCameraManager->GetCameraLocation() - Focus;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Rel.Y, Rel.X));

		if (bOrbitHasPrevYaw)
		{
			OrbitActualDegrees += FMath::Abs(FMath::FindDeltaAngleDegrees(OrbitPrevYaw, Yaw));
		}

		OrbitPrevYaw = Yaw;
		bOrbitHasPrevYaw = true;

		OrbitMinHeight = FMath::Min(OrbitMinHeight, static_cast<float>(Rel.Z));
		OrbitMaxHeight = FMath::Max(OrbitMaxHeight, static_cast<float>(Rel.Z));

		if (CaptureOrbitDebug::Enabled)
		{
			OrbitTrail.Add(PC->PlayerCameraManager->GetCameraLocation());
		}
	}

	if (!CaptureOrbitDebug::Enabled)
		return;

#if ENABLE_DRAW_DEBUG
	using CapturePresentation::DebugSpiralSegments;

	const FACFCameraSequenceState State = Cameras->GetCameraSequenceState(ActiveFocus);
	const UACFCameraSequence* Sequence = ActiveFocusSequence;
	const UACFCameraNode_SpiralOrbit* Orbit = FindSpiralOrbit(Sequence);
	UWorld* World = GetWorld();

	DrawDebugSphere(World, Focus, 12.f, 8, FColor::Cyan, false, 0.f, SDPG_Foreground);

	FACFCameraSpiralSample Ideal;

	if (Orbit)
	{
		// 計画した螺旋（黄）と、その時刻の理想位置（緑の点）。実カメラの軌跡は白。
		FVector Prev = Orbit->SampleSpiral(Focus, OrbitStartYaw, 0.f).Location;

		for (int32 I = 1; I <= DebugSpiralSegments; ++I)
		{
			const float SampleTime = Orbit->Duration * I / static_cast<float>(DebugSpiralSegments);
			const FVector P = Orbit->SampleSpiral(Focus, OrbitStartYaw, SampleTime).Location;
			DrawDebugLine(World, Prev, P, FColor::Yellow, false, 0.f, SDPG_Foreground, 1.f);
			Prev = P;
		}

		Ideal = Orbit->SampleSpiral(Focus, OrbitStartYaw, State.ShotTime);
		DrawDebugPoint(World, Ideal.Location, 10.f, FColor::Green, false, 0.f, SDPG_Foreground);
	}

	for (int32 I = 1; I < OrbitTrail.Num(); ++I)
	{
		DrawDebugLine(World, OrbitTrail[I - 1], OrbitTrail[I], FColor::White, false, 0.f, SDPG_Foreground, 1.f);
	}

	// VFXの段階（命中からの実時間）。
	const float TeleportStart = GetTeleportStart();
	const float DataStart = HitStopDuration + SlowDuration;
	const float Commit = GetTotalDuration();

	const TCHAR* VFXPhase = TEXT("Done");

	if (OrbitRealAge < TeleportStart)
	{
		VFXPhase = TEXT("Impact");
	}
	else if (OrbitRealAge < DataStart)
	{
		VFXPhase = TEXT("Spiral (envelop)");
	}
	else if (OrbitRealAge < Commit)
	{
		VFXPhase = TEXT("Pillar + dissolve");
	}
	else if (OrbitRealAge < Commit + TeleportTailDuration)
	{
		VFXPhase = TEXT("Tail (particles)");
	}

	const TCHAR* CameraPhase = TEXT("Hold (return next)");

	if (!State.bInControl)
	{
		CameraPhase = TEXT("Waiting / interrupted");
	}
	else if (Orbit && State.ShotTime < Orbit->Duration)
	{
		CameraPhase = TEXT("Orbit");
	}

	if (GEngine)
	{
		// Component毎に固定のKeyを使い、画面上の表示を毎フレーム上書きする。
		const uint64 MessageKey = static_cast<uint64>(reinterpret_cast<UPTRINT>(this));
		GEngine->AddOnScreenDebugMessage(MessageKey, 0.f, FColor::Cyan, FString::Printf(
			TEXT("[CaptureOrbit] %s  handle=%s shot=%d t=%.2f/%.2f  progress=%.0f%%  angle(ideal)=%.0f/%.0f deg  angle(actual)=%.0f deg\n")
			TEXT("radius(ideal)=%.0f  height(ideal)=%.0f  height(actual)=%.0f..%.0f  focus=%s  VFX=%s"),
			CameraPhase, *ActiveFocus.ToString(), State.ShotIndex, State.ShotTime, State.ShotDuration, Ideal.Progress * 100.f,
			Ideal.AngleTravelled, Orbit ? Orbit->RotationDegrees : 0.f, OrbitActualDegrees, Ideal.Radius, Ideal.Height,
			OrbitMinHeight, OrbitMaxHeight, *Focus.ToCompactString(), VFXPhase));
	}
#endif
}

void UCapturePresentationComponent::UpdateLingering(float RealDelta)
{
	for (int32 I = Lingering.Num() - 1; I >= 0; --I)
	{
		FCaptureLingeringVFX& L = Lingering[I];
		L.Age += RealDelta;

		if (IsValid(L.VFX))
		{
			L.VFX->SetDesiredAge(L.Age);
		}

		if (!IsValid(L.VFX) || L.Age >= L.EndAge)
		{
			if (IsValid(L.VFX))
			{
				L.VFX->DestroyComponent();
			}

			Lingering.RemoveAt(I);
		}
	}
}

void UCapturePresentationComponent::DestroyLingering()
{
	for (FCaptureLingeringVFX& L : Lingering)
	{
		if (IsValid(L.VFX))
		{
			L.VFX->DestroyComponent();
		}
	}

	Lingering.Reset();
}

float UCapturePresentationComponent::GetTeleportStart() const
{
	return FMath::Max(0.f, HitStopDuration + SlowDuration - TeleportLeadTime);
}

void UCapturePresentationComponent::StartTeleport(FCaptureVisualTarget& T)
{
	using namespace CapturePresentation;

	T.bTeleportStarted = true;

	if (!TeleportVFX || !T.Boar.IsValid())
		return;

	// 足元を原点に、Boarの大きさをUser Parameterで渡す。Actorには付けない（Boarが消えても位置が残る）。
	FVector Foot = T.CaptureLocation;
	float Radius = TeleportDefaultRadius;
	float Height = TeleportDefaultHeight;

	if (T.Mesh.IsValid())
	{
		const FBoxSphereBounds Bounds = T.Mesh->Bounds;
		Foot = FVector(Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z - Bounds.BoxExtent.Z);
		Radius = FMath::Max(TeleportMinRadius, FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y));
		Height = FMath::Max(TeleportMinHeight, Bounds.BoxExtent.Z * 2.f);
	}

	T.TeleportVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TeleportVFX, Foot, FRotator::ZeroRotator,
		FVector::OneVector, false, false);
	if (!T.TeleportVFX)
		return;

	T.TeleportVFX->SetVariableLinearColor(TEXT("User.Color"), TeleportColor);
	T.TeleportVFX->SetVariableLinearColor(TEXT("User.CoreColor"), TeleportCoreColor);
	T.TeleportVFX->SetVariableFloat(TEXT("User.Radius"), Radius * TeleportVFXScale);
	T.TeleportVFX->SetVariableFloat(TEXT("User.Height"), Height * TeleportVFXScale);

	// Niagara内の時刻：0=テレポート開始、User.CommitTime=捕獲確定（Boar消失・Cage収容。Gameplayの時刻のまま）、
	// その後User.TailDurationで光柱が弱まり粒子が上空へ抜ける。
	const float CommitTime = FMath::Max(TeleportMinCommitTime, GetTotalDuration() - GetTeleportStart());
	T.TeleportVFX->SetVariableFloat(TEXT("User.CommitTime"), CommitTime);
	T.TeleportVFX->SetVariableFloat(TEXT("User.TransferDuration"), CommitTime);
	T.TeleportVFX->SetVariableFloat(TEXT("User.TailDuration"), TeleportTailDuration);

	// HitStop・Slow中もリングと同じ実時間で進める。年齢はUpdateTarget（確定後はUpdateLingering）から指定する。
	T.TeleportVFX->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
	T.TeleportVFX->SetCanRenderWhileSeeking(true);
	T.TeleportVFX->Activate(true);
}

// 捕獲地点から半径内の未捕獲Boarを減速対象に加える。捕獲されたBoarは発光・データ化の演出側で扱うため含めない。
void UCapturePresentationComponent::CollectLocalSlowTargets(const FVector& Center)
{
	if (LocalSlowRadius <= 0.f || !GetWorld())
		return;

	const float RadiusSquared = FMath::Square(LocalSlowRadius);

	for (TActorIterator<ABoarBase> It(GetWorld()); It; ++It)
	{
		ABoarBase* Boar = *It;
		if (!IsValid(Boar) || Boar->IsCaptured() || FVector::DistSquared(Boar->GetActorLocation(), Center) > RadiusSquared)
			continue;

		if (SlowedBoars.ContainsByPredicate([Boar](const FCaptureSlowedBoar& S) { return S.Boar == Boar; }))
			continue;

		FCaptureSlowedBoar Slowed;
		Slowed.Boar = Boar;
		SlowedBoars.Add(Slowed);
	}
}

// アニメーションはMeshのGlobalAnimRateScale、移動はUBoarMovementComponentの移動時間倍率で減速する。
// MaxWalkSpeedは変えないので、ApplyMovementSpeedや攻撃Taskの速度設定・保存と競合せず、毎Tickの再計算でも打ち消されない。
void UCapturePresentationComponent::ApplyLocalSlow(bool bHitStop)
{
	using CapturePresentation::MinSlowRate;

	const float AnimRate = FMath::Clamp(bHitStop ? HitStopAnimRate : SlowAnimRate, MinSlowRate, 1.f);
	const float MoveScale = bMatchMovementToAnimation
		? AnimRate
		: FMath::Clamp(bHitStop ? HitStopMoveScale : SlowMoveScale, MinSlowRate, 1.f);

	for (int32 I = SlowedBoars.Num() - 1; I >= 0; --I)
	{
		FCaptureSlowedBoar& S = SlowedBoars[I];
		ABoarBase* Boar = S.Boar.Get();
		if (!IsValid(Boar))
		{
			SlowedBoars.RemoveAt(I);
			continue;
		}

		// 演出中に捕獲されたBoarは元の速さへ戻し、以後は捕獲演出だけに任せる。
		if (Boar->IsCaptured())
		{
			RestoreSlowedBoar(S);
			SlowedBoars.RemoveAt(I);
			continue;
		}

		if (USkeletalMeshComponent* Mesh = Boar->GetMesh(); Mesh && !S.bAnimOwnershipLost)
		{
			if (!S.bAnimApplied)
			{
				// 初回: 元の倍率を控える。
				S.OriginalAnimRate = Mesh->GlobalAnimRateScale;
			}
			else if (!FMath::IsNearlyEqual(Mesh->GlobalAnimRateScale, S.AppliedAnimRate))
			{
				// 自分が設定した値から変わっている = 他の処理が倍率を変えた。以後は触らない。
				S.bAnimOwnershipLost = true;
			}

			if (!S.bAnimOwnershipLost)
			{
				Mesh->GlobalAnimRateScale = S.OriginalAnimRate * AnimRate;
				S.AppliedAnimRate = Mesh->GlobalAnimRateScale;
				S.bAnimApplied = true;
			}
			else
			{
				S.bAnimApplied = false;
			}
		}

		if (auto* Movement = Cast<UBoarMovementComponent>(Boar->GetCharacterMovement()))
		{
			Movement->SetPresentationTimeScale(MoveScale);
			S.bMoveApplied = true;
		}
	}
}

// 1体分の復元。自分が設定した値のまま残っている場合だけ戻す（他の処理が変えたアニメーション倍率は上書きしない）。
void UCapturePresentationComponent::RestoreSlowedBoar(FCaptureSlowedBoar& S)
{
	ABoarBase* Boar = S.Boar.Get();

	if (S.bAnimApplied && IsValid(Boar) && Boar->GetMesh()
		&& FMath::IsNearlyEqual(Boar->GetMesh()->GlobalAnimRateScale, S.AppliedAnimRate))
	{
		Boar->GetMesh()->GlobalAnimRateScale = S.OriginalAnimRate;
	}

	// 移動時間倍率を設定するのは捕獲演出だけなので、通常値へ戻す。
	if (S.bMoveApplied && IsValid(Boar))
	{
		if (auto* Movement = Cast<UBoarMovementComponent>(Boar->GetCharacterMovement()))
		{
			Movement->SetPresentationTimeScale(1.f);
		}
	}

	S.bAnimApplied = false;
	S.bMoveApplied = false;
}

// 全対象を復元する。対象リストは残すので、Pause解除後は同じBoarへ再び適用できる（次の共有演出の開始時に作り直す）。
void UCapturePresentationComponent::ReleaseLocalSlow()
{
	for (FCaptureSlowedBoar& S : SlowedBoars)
	{
		RestoreSlowedBoar(S);
	}
}

void UCapturePresentationComponent::ApplyFieldOfView(float FOV)
{
	if (!Camera.IsValid())
		return;

	Camera->SetFieldOfView(FOV);
	AppliedFOV = Camera->FieldOfView;
	bOwnCamera = true;
}

void UCapturePresentationComponent::ReleaseCamera()
{
	if (!bOwnCamera)
		return;

	bOwnCamera = false;

	// 自分が設定したFOVのまま残っている場合だけ元へ戻す。
	if (Camera.IsValid() && FMath::IsNearlyEqual(Camera->FieldOfView, AppliedFOV))
	{
		Camera->SetFieldOfView(OriginalFOV);
	}
}

void UCapturePresentationComponent::ReleaseCameraShake()
{
	if (!ActiveShake)
		return;

	if (auto* PC = Cast<APlayerController>(GetOwner()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StopCameraShake(ActiveShake, true);
		}
	}

	ActiveShake = nullptr;
}

// 共有状態の解放。正常終了・キャンセル・対象Destroy・Pause・EndPlayのどこから何度呼ばれても安全。
void UCapturePresentationComponent::RestoreSharedState(bool bForPause)
{
	// Orbitカメラ（ActiveFocus）はここでは止めない。共有演出（約1.1秒）より長く回り、Shotの終わりにACFのReturn Blendで
	// 自然に戻る（UpdateFocusCameraが終了を検出）。止めるのはCancelPresentation・EndPlayだけ。Pause中はACFも止まる。
	(void)bForPause;

	ReleaseLocalSlow();
	ReleaseCamera();
	ReleaseCameraShake();

	// Stage終了カメラ・Pause・キャンセルへ残さないよう即座に止める（正常終了時は既にEnvelopeが終わっている）。
	ReleaseCameraEffect(true);
	bUsingACFEffect = false;
}

// 対象ごとの見た目の復元とVFX破棄。一度だけ実行し、生成物の参照も手放す。
void UCapturePresentationComponent::RestoreTarget(FCaptureVisualTarget& T, bool bKeepTeleportTail)
{
	// 正常な確定（とBoarの消滅）では、テレポートVFXだけを余韻用に引き継ぐ（光柱が弱まり粒子が上空へ抜けるまで）。
	if (bKeepTeleportTail && !T.bRestored && IsValid(T.TeleportVFX))
	{
		FCaptureLingeringVFX L;
		L.VFX = T.TeleportVFX;
		L.Age = FMath::Max(0.f, T.Age - GetTeleportStart());
		L.EndAge = FMath::Max(L.Age, GetTotalDuration() - GetTeleportStart() + TeleportTailDuration);
		Lingering.Add(L);
		T.TeleportVFX = nullptr;
	}

	if (T.bRestored)
		return;

	T.bRestored = true;

	if (T.Mesh.IsValid() && T.Glow && T.Mesh->GetOverlayMaterial() == T.Glow)
	{
		T.Mesh->SetOverlayMaterial(T.OriginalOverlay);
	}

	if (T.Mesh.IsValid())
	{
		T.Mesh->SetVisibility(T.bOriginalVisible, false);
	}

	if (IsValid(T.DataMesh))
	{
		T.DataMesh->DestroyComponent();
	}

	if (IsValid(T.DataVFX))
	{
		T.DataVFX->DestroyComponent();
	}

	if (IsValid(T.VFX))
	{
		T.VFX->DestroyComponent();
	}

	if (IsValid(T.TeleportVFX))
	{
		T.TeleportVFX->DestroyComponent();
	}

	T.DataMesh = nullptr;
	T.DataVFX = nullptr;
	T.VFX = nullptr;
	T.TeleportVFX = nullptr;

	if (T.Boar.IsValid())
	{
		T.Boar->OnDestroyed.RemoveDynamic(this, &ThisClass::OnTargetDestroyed);
	}
}

void UCapturePresentationComponent::CancelPresentation(bool bCommitCaptures)
{
	const auto* Stage = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr;

	// 最後の捕獲でClearになった場合も、確定済みの短い和音は自然に終える。
	StopCaptureAudio(bCommitCaptures && Stage && Stage->GetStageState() == EBoarStageState::Cleared);

	// 先に演出リストと周囲Boarの減速を片付け、Gameplayへの通知が再入しても同じ対象を確定しない。
	// Clear・GameOver・Retry・Level破棄ではFocusカメラをカットで外し、次のカメラ（勝利カメラ等）へ持ち越さない。
	ReleaseFocusCamera(true);
	RestoreSharedState();
	bSharedActive = false;

	TArray<TWeakObjectPtr<ABoarBase>> Pending;

	for (auto& T : Targets)
	{
		Pending.Add(T.Boar);
		RestoreTarget(T);
	}

	Targets.Reset();

	for (auto& R : Receptions)
	{
		if (IsValid(R.VFX))
		{
			R.VFX->DestroyComponent();
		}
	}

	Receptions.Reset();

	DestroyLingering();

	// 確定待ちの所有者はGameMode。bCommitCaptures時は確定を依頼し（Stage終了後はGameMode側の判定で確定されない）、
	// そうでなければ（Level破棄・見た目だけの片付け）確定せずに確定待ちから外す。
	auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr;
	if (!IsValid(Mode) || Pending.IsEmpty())
		return;

	if (!bCommitCaptures)
	{
		for (const TWeakObjectPtr<ABoarBase>& Boar : Pending)
		{
			Mode->DiscardPendingCapture(Boar);
		}

		return;
	}

	bCommittingCaptures = true;
	Mode->CompletePendingCaptures(Pending);
	bCommittingCaptures = false;
}

void UCapturePresentationComponent::OnTargetDestroyed(AActor* Actor)
{
	auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr;

	for (int32 I = Targets.Num() - 1; I >= 0; --I)
	{
		if (Targets[I].Boar.Get() == Actor || !Targets[I].Boar.IsValid())
		{
			// 消えたBoarの捕獲は確定しない。確定待ちからも外す。
			if (IsValid(Mode))
			{
				Mode->DiscardPendingCapture(Targets[I].Boar);
			}

			RestoreTarget(Targets[I], true);
			Targets.RemoveAt(I);
		}
	}

	if (Targets.IsEmpty())
	{
		if (!CaptureAudioTargets.IsEmpty())
		{
			StopCaptureAudio();
		}

		RestoreSharedState();
		bSharedActive = false;
	}
}

void UCapturePresentationComponent::StartDataConversion(FCaptureVisualTarget& T)
{
	T.bDataStarted = true;

	ABoarBase* Boar = T.Boar.Get();
	USkeletalMeshComponent* Source = T.Mesh.Get();
	if (!Boar || !Source)
		return;

	if (DataMaterial && Source->GetSkeletalMeshAsset())
	{
		// 元のMeshと同じ姿勢で動く残像Proxyを作り、DataMaterialで描く（元のMeshとMaterialは変更しない）。
		USkeletalMeshComponent* Proxy = NewObject<USkeletalMeshComponent>(Boar, NAME_None, RF_Transient);
		Proxy->SetMobility(EComponentMobility::Movable);
		Proxy->SetupAttachment(Source->GetAttachParent() ? Source->GetAttachParent() : Boar->GetRootComponent());
		Proxy->SetRelativeTransform(Source->GetRelativeTransform());
		Proxy->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
		Proxy->SetLeaderPoseComponent(Source);
		Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Proxy->SetGenerateOverlapEvents(false);
		Proxy->SetCanEverAffectNavigation(false);
		Proxy->SetCastShadow(false);

		T.DataMaterial = UMaterialInstanceDynamic::Create(DataMaterial, this);
		T.DataMaterial->SetVectorParameterValue(TEXT("GlowColor"), CaptureColor);
		T.DataMaterial->SetScalarParameterValue(TEXT("CaptureMinZ"), T.CaptureMinZ);
		T.DataMaterial->SetScalarParameterValue(TEXT("CaptureHeight"), T.CaptureHeight);
		T.DataMaterial->SetScalarParameterValue(TEXT("DissolveProgress"), 0.f);

		for (int32 Slot = 0; Slot < FMath::Max(1, Proxy->GetNumMaterials()); ++Slot)
		{
			Proxy->SetMaterial(Slot, T.DataMaterial);
		}

		Proxy->RegisterComponent();
		T.DataMesh = Proxy;
		Source->SetVisibility(false, false);
	}

	// 上昇するデータ粒子はNiagaraが生成・移動・消失させる。C++は開始と、実時間の進行・終了時刻・色だけを渡す。
	if (!DataFragmentVFX)
		return;

	T.DataVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DataFragmentVFX, T.CaptureLocation,
		FRotator::ZeroRotator, FVector::OneVector, false, false);
	if (!T.DataVFX)
		return;

	T.DataVFX->SetVariableLinearColor(TEXT("User.Color"), CaptureColor);
	T.DataVFX->SetVariableFloat(TEXT("User.FragmentEndTime"), CaptureDissolveDuration + TransferFinishDuration);
	T.DataVFX->SetVariableFloat(TEXT("User.FragmentFadeTime"), TransferFinishDuration);

	// HitStop・SlowMotion中もリングと同じ実時間で進めるため、年齢はOnWorldTickから指定する。
	T.DataVFX->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
	T.DataVFX->SetCanRenderWhileSeeking(true);
	T.DataVFX->Activate(true);
}

void UCapturePresentationComponent::StartCageReception(const FVector& Location)
{
	using namespace CapturePresentation;

	// 未設定時は別アセットで代用せずスキップする（BeginPlayでError報告済み）。
	UNiagaraSystem* System = CageReceptionVFX.Get();
	if (!System)
		return;

	FCaptureReception R;
	R.VFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System,
		Location - FVector(0, 0, CaptureRingHeightOffset),
		FRotator::ZeroRotator, FVector(VFXScale * ReceptionSpawnScale), false, false);

	if (R.VFX)
	{
		R.VFX->SetVariableLinearColor(TEXT("User.Color"),
			FLinearColor(ReceptionColor.R, ReceptionColor.G, ReceptionColor.B, ReceptionAlpha));
		R.VFX->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
		R.VFX->SetCanRenderWhileSeeking(true);
		R.VFX->Activate(true);
		Receptions.Add(R);
	}
}

void UCapturePresentationComponent::OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld())
		return;

	// Hit Stop・Slow中もWorldの時間は止めないが、演出は常に実時間で進める。
	const double Now = World->GetRealTimeSeconds();
	const float RealDelta = FMath::Max(0.f, float(Now - LastRealTime));
	LastRealTime = Now;

	// Orbitカメラと余韻VFXは捕獲確定の後も続くので、それらが残っている間も進める（Stage終了・中断で確実に止めるため）。
	const bool bNothingToUpdate = Targets.IsEmpty() && Receptions.IsEmpty() && Lingering.IsEmpty()
		&& !ActiveFocus.IsValid() && !IsValid(FocusProxy) && !CaptureConfirmVoice.IsValid();

	if (bNothingToUpdate)
		return;

	auto* Mode = World->GetAuthGameMode<ABoarGameMode>();
	if (Mode && Mode->GetStageState() != EBoarStageState::Playing)
	{
		CancelPresentation();
		return;
	}

	// Pause中は演出を進めないが、周囲のBoarやカメラを減速したままにはしない。
	if (UGameplayStatics::IsGamePaused(this))
	{
		RestoreSharedState(true);
		return;
	}

	auto* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->GetPawn())
	{
		CancelPresentation();
		return;
	}

	// 実行順：共有演出 → 個別対象 → 共有状態の解放 → 捕獲確定 → Cage受信。順序は従来どおり。
	if (bSharedActive)
	{
		UpdateSharedFeedback(RealDelta);
	}

	if (!CaptureAudioTargets.IsEmpty())
	{
		UpdateCaptureAudio(RealDelta);
	}

	TArray<TWeakObjectPtr<ABoarBase>> Finished;
	UpdateTargets(RealDelta, Finished);

	// Gameplayへ通知する前に共有状態（局所減速・カメラ）を戻す。この通知から既存のClear処理が始まることがあるため。
	if (Targets.IsEmpty())
	{
		RestoreSharedState();
		bSharedActive = false;
	}

	CommitFinishedCaptures(Mode, Finished);

	if (Targets.IsEmpty() && !CaptureAudioTargets.IsEmpty())
	{
		StopCaptureAudio();
	}

	UpdateReceptions(RealDelta, Mode);
	UpdateLingering(RealDelta);
	UpdateFocusCamera(RealDelta);
}

// Hit Stop・Slow・Camera Punch In・効果音（Net 1回につき1組）。
void UCapturePresentationComponent::UpdateSharedFeedback(float RealDelta)
{
	using namespace CapturePresentation;

	SharedAge += RealDelta;

	// Hit Stop → Slow → 通常速度。毎回値を設定し直すので、Pause解除後も同じ段階の減速へ戻る。
	if (SharedAge < HitStopDuration + SlowDuration)
	{
		ApplyLocalSlow(SharedAge < HitStopDuration);
	}
	else
	{
		ReleaseLocalSlow();
	}

	if (Camera.IsValid())
	{
		// Sinカーブで一度狭めてから元の視野角へ戻す。
		const float P = FMath::Clamp(SharedAge / FMath::Max(MinDuration, CameraDuration), 0.f, 1.f);
		ApplyFieldOfView(FMath::Clamp(OriginalFOV - CameraZoomAmount * FMath::Sin(P * PI), MinFieldOfView, MaxFieldOfView));
	}

	const float DataStart = HitStopDuration + SlowDuration;
	if (!bTransferSoundPlayed && SharedAge >= DataStart)
	{
		if (!bCaptureAudioV2)
		{
			PlaySound(TransferStartSound);
		}

		bTransferSoundPlayed = true;
	}

	if (!bConfirmed && SharedAge >= GetTotalDuration())
	{
		if (!bCaptureAudioV2)
		{
			PlaySound(CaptureSuccessSound, bMultiFeedbackPlayed ? MultiCaptureSuccessPitch : 1.f);
		}

		bConfirmed = true;
	}

	if (SharedAge >= FMath::Max(GetTotalDuration(), CameraDuration))
	{
		RestoreSharedState();
		bSharedActive = false;
	}
}

// 全対象を更新し、演出を終えた対象を後片付けしてOutFinishedへ（捕獲順を保って）入れる。
void UCapturePresentationComponent::UpdateTargets(float RealDelta, TArray<TWeakObjectPtr<ABoarBase>>& OutFinished)
{
	for (int32 I = Targets.Num() - 1; I >= 0; --I)
	{
		auto& T = Targets[I];
		if (!UpdateTarget(T, RealDelta))
			continue;

		OutFinished.Insert(T.Boar, 0);
		RestoreTarget(T, true);
		Targets.RemoveAt(I);
	}
}

// 1体分の発光・リング・データ化・データ粒子を進める。演出が終わった（または対象が無効になった）らtrue。
bool UCapturePresentationComponent::UpdateTarget(FCaptureVisualTarget& T, float RealDelta)
{
	using namespace CapturePresentation;

	T.Age += RealDelta;
	const float DataStart = HitStopDuration + SlowDuration;
	const float P = FMath::Clamp(T.Age / FMath::Max(MinDuration, DataStart), 0.f, 1.f);

	// ---- 発光 ----
	if (T.Glow)
	{
		const float Envelope = FMath::Lerp(GlowAmountStart, GlowAmountPeak, FMath::SmoothStep(0.f, 1.f, P));
		FLinearColor Color = FMath::Lerp(GlowStartColor, GlowPeakColor, P);
		float Amount = Envelope;

		// 命中直後の白いフラッシュから通常の発光へ戻す。
		if (HitFlashDuration > 0.f && T.Age < HitFlashDuration)
		{
			const float F = FMath::SmoothStep(0.f, 1.f, T.Age / HitFlashDuration);
			Amount = FMath::Lerp(HitFlashGlowAmount, Envelope, F);
			Color = FMath::Lerp(HitFlashColor, Color, F);
		}

		T.Glow->SetScalarParameterValue(TEXT("GlowAmount"), Amount);
		T.Glow->SetVectorParameterValue(TEXT("GlowColor"), Color);
	}

	// ---- 捕獲リング（広がってから、転送完了に向けて薄くなる） ----
	if (IsValid(T.VFX))
	{
		T.VFX->SetDesiredAge(T.Age);

		const float RingEnvelope = FMath::Clamp(
			(GetTotalDuration() - T.Age) / FMath::Max(MinDuration, TransferFinishDuration), 0.f, 1.f);
		const float RingExpand = FMath::Clamp(T.Age / RingExpandTime, 0.f, 1.f);

		T.VFX->SetWorldScale3D(FVector(VFXScale * FMath::Lerp(RingScaleStart, RingScaleEnd, RingExpand)));
		T.VFX->SetVariableLinearColor(TEXT("User.Color"),
			FLinearColor(CaptureColor.R, CaptureColor.G, CaptureColor.B, RingAlpha * RingEnvelope));
	}

	// ---- 青白いテレポート光：データ化の少し前に出し、転送完了（捕獲確定）で終わる ----
	if (!T.bTeleportStarted && T.Age >= GetTeleportStart())
	{
		StartTeleport(T);
	}

	if (IsValid(T.TeleportVFX))
	{
		T.TeleportVFX->SetDesiredAge(FMath::Max(0.f, T.Age - GetTeleportStart()));
	}

	// ---- データ化（Dissolve → 上昇しながら吸い込まれる） ----
	if (T.Age >= DataStart && !T.bDataStarted)
	{
		StartDataConversion(T);
	}

	if (T.bDataStarted)
	{
		const float DataAge = FMath::Max(0.f, T.Age - DataStart);
		const float DissolveP = FMath::Clamp(DataAge / FMath::Max(MinDuration, CaptureDissolveDuration), 0.f, 1.f);
		const float FinishP = FMath::Clamp(
			(DataAge - CaptureDissolveDuration) / FMath::Max(MinDuration, TransferFinishDuration), 0.f, 1.f);

		if (T.DataMaterial)
		{
			const float DissolveProgress = FMath::Lerp(0.f, DissolveProgressAtDissolveEnd, DissolveP)
				+ DissolveProgressFinishAdd * FinishP;

			T.DataMaterial->SetScalarParameterValue(TEXT("DissolveProgress"), DissolveProgress);
			T.DataMaterial->SetScalarParameterValue(TEXT("GlowAmount"), 1.f + DataGlowExtra * (1.f - DissolveP));
		}

		if (IsValid(T.DataMesh) && T.Mesh.IsValid())
		{
			T.DataMesh->SetRelativeLocation(T.Mesh->GetRelativeLocation() + FVector(0, 0, TransferRiseDistance * FinishP));

			// 上昇しながら横方向に細く吸い込まれる（残像Proxyだけを変形し、元のMeshは変えない）。
			const float Width = FMath::Lerp(1.f, TransferEndWidthScale, FMath::SmoothStep(0.f, 1.f, FinishP));
			const FVector Base = T.Mesh->GetRelativeScale3D();
			T.DataMesh->SetRelativeScale3D(FVector(Base.X * Width, Base.Y * Width, Base.Z));
		}

		if (IsValid(T.DataVFX))
		{
			T.DataVFX->SetDesiredAge(DataAge);
		}
	}

	return !T.Boar.IsValid() || !T.Boar->IsCaptured() || T.Age >= GetTotalDuration();
}

// 演出を終えた対象をGameModeへ通知し、確定（収容）できたBoarにだけCage受信演出を出す。
// 確定可否・二重確定防止・収容数の再集計はGameModeが担当する（タイミングは従来どおり）。
void UCapturePresentationComponent::CommitFinishedCaptures(ABoarGameMode* Mode,
	const TArray<TWeakObjectPtr<ABoarBase>>& Finished)
{
	if (!Mode || Finished.IsEmpty())
		return;

	// 確定前の位置を控え、収容で移動したBoarだけを受信演出の対象にする。
	TMap<TWeakObjectPtr<ABoarBase>, FVector> CaptureLocations;

	for (const TWeakObjectPtr<ABoarBase>& Boar : Finished)
	{
		if (Boar.IsValid() && Boar->IsCaptured())
		{
			CaptureLocations.Add(Boar, Boar->GetActorLocation());
		}
	}

	// 確定通知中にClear要求などで状態が変わっても、HasPendingCaptures()は通知中をtrueで返す。
	// 通知の中でこのComponentが破棄されることがあるため、戻ったら生存を確認する。
	const TWeakObjectPtr<UCapturePresentationComponent> Self(this);
	bCommittingCaptures = true;
	const TArray<ABoarBase*> Confirmed = Mode->CompletePendingCaptures(Finished);

	if (!Self.IsValid())
		return;

	bCommittingCaptures = false;

	ConfirmCaptureAudio(Confirmed);

	for (ABoarBase* Boar : Confirmed)
	{
		const FVector* Before = CaptureLocations.Find(Boar);
		if (Before && IsValid(Boar) && Boar->IsCaptured()
			&& !Boar->GetActorLocation().Equals(*Before, CapturePresentation::HousedMoveTolerance))
		{
			StartCageReception(Boar->GetActorLocation());
		}
	}
}

// Cage受信エフェクトを進め、終わったものを破棄する。全演出が終わったら収容数を再通知する。
void UCapturePresentationComponent::UpdateReceptions(float RealDelta, ABoarGameMode* Mode)
{
	using namespace CapturePresentation;

	const bool bHadReceptions = !Receptions.IsEmpty();

	for (int32 I = Receptions.Num() - 1; I >= 0; --I)
	{
		FCaptureReception& R = Receptions[I];
		R.Age += RealDelta;

		if (IsValid(R.VFX))
		{
			R.VFX->SetDesiredAge(R.Age);

			// 広がりながら薄くなる。
			const float Fade = 1.f - FMath::Clamp(R.Age / FMath::Max(MinDuration, CageReceptionDuration), 0.f, 1.f);
			R.VFX->SetVariableLinearColor(TEXT("User.Color"),
				FLinearColor(ReceptionColor.R, ReceptionColor.G, ReceptionColor.B, ReceptionAlpha * Fade));
			R.VFX->SetWorldScale3D(FVector(VFXScale * FMath::Lerp(ReceptionScaleStart, ReceptionScaleEnd, 1.f - Fade)));
		}

		if (R.Age >= CageReceptionDuration || !IsValid(R.VFX))
		{
			if (IsValid(R.VFX))
			{
				R.VFX->DestroyComponent();
			}

			Receptions.RemoveAt(I);
		}
	}

	if (bHadReceptions && Receptions.IsEmpty() && Targets.IsEmpty() && Mode)
	{
		Mode->RefreshHousedBoarCount();
	}
}
