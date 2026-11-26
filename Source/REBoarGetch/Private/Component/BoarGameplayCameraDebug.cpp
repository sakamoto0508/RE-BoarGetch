// UBoarGameplayCameraComponentの診断機能（Boar.Camera.Dump／Trace）です。ゲームプレイ処理は BoarGameplayCameraComponent.cpp にあります。
// 計測・Traceの本体はShipping以外でだけ有効です（Dumpの基本情報だけはShippingでも出力します）。
#include "Component/BoarGameplayCameraComponent.h"
#include "BoarGameplayCameraInternal.h"

#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraDirector.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraTuning.h"
#include "ACFCameraTuningLibrary.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/UObjectIterator.h"

namespace BoarGameplayCamera
{
	const TCHAR* DampingSpaceName(EACFCameraDampingSpace Space)
	{
		switch (Space)
		{
		case EACFCameraDampingSpace::World: return TEXT("World");
		case EACFCameraDampingSpace::TargetLocal: return TEXT("TargetLocal");
		case EACFCameraDampingSpace::CameraYaw: return TEXT("CameraYaw");
		}
		return TEXT("Unknown");
	}

	/** ACFの出力Weightがこれ以上なら、ブレンドなしでACFが画面を握っているとみなします。 */
	static constexpr float FullWeightThreshold = 0.999f;

	/** 描画カメラとACF姿勢の差がこれ以内なら「画面はACFの結果」と判定します（cm / 度）。 */
	static constexpr float RenderedMatchDistance = 1.f;
	static constexpr float RenderedMatchAngleDeg = 0.5f;

#if !UE_BUILD_SHIPPING
	// ---- Trace（Damping計測）の判定しきい値 ----

	/** 移動中とみなす3D速度（cm/s）。この間のPivot遅れ時間を集計します。 */
	static constexpr float MovingSpeed = 100.f;

	/** 旋回中とみなすYaw速度（度/s）。この間の回転遅れ時間を集計します。 */
	static constexpr float TurningYawRate = 60.f;

	/** 停止後の収束計測: この速度以上で「移動していた」、未満で「停止した」とみなします（cm/s）。 */
	static constexpr float DecayMovingSpeed = 200.f;
	static constexpr float DecayStoppedSpeed = 10.f;

	/** 収束計測中にこの速度を超えたら再び動き出したとみなして計測を捨てます（cm/s）。 */
	static constexpr float DecayAbortSpeed = 50.f;

	/** 停止時のPivot誤差がこれ未満なら収束計測をしません（cm）。 */
	static constexpr float DecayMinPivotError = 1.f;

	/** 旋回停止とみなすYaw速度（度/s）と、収束計測をする最小の回転誤差（度）です。 */
	static constexpr float DecayStoppedYawRate = 5.f;
	static constexpr float DecayMinRotError = .5f;

	/** 回転の収束計測中にこのYaw速度を超えたら計測を捨てます（度/s）。 */
	static constexpr float DecayAbortYawRate = 20.f;

	/** 1/e（約37%）。誤差が停止時のこの割合まで減った時間を「収束時間」とします。 */
	static constexpr float InverseE = 0.36788f;

	/** Trace開始時に確保しておくCSV行数です（約2分 @60fps）。 */
	static constexpr int32 TraceRowReserve = 8192;
#endif

#if !UE_BUILD_SHIPPING
	/** 1フレーム分の計測値。ACFのRig Debug（Shipping以外）とPreset設定値から計算します。 */
	struct FSample
	{
		bool bACFMode = false;
		bool bOurRequestActive = false;
		bool bBlending = false;
		bool bHasRig = false; // Orbit / Collision のDebugが取れた
		float Weight = 0.f;
		float RequestWeight = 0.f;
		float BlendAlpha = 1.f;
		FVector Target = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector IdealPivot = FVector::ZeroVector;
		FVector DampedPivot = FVector::ZeroVector;
		FVector PivotError = FVector::ZeroVector;       // Damped - Ideal（World）
		FVector PivotErrorCamera = FVector::ZeroVector; // X = カメラ前方、Y = 右、Z = 上（OrbitのYaw基準）
		FRotator ControlRotation = FRotator::ZeroRotator;
		FRotator OrbitRotation = FRotator::ZeroRotator;
		float RotErrorYaw = 0.f; // Control - Orbit（度）
		float RotErrorPitch = 0.f;
		bool bCollisionHit = false;
		float DesiredDistance = 0.f;
		float ActualDistance = 0.f;
		FVector Rendered = FVector::ZeroVector;
		FVector ACFPose = FVector::ZeroVector; // ACFの評価結果（Output Modifierが書く値）
		bool bHasACFPose = false;
		FVector SpringArmCamera = FVector::ZeroVector;
		FVector ShoulderOffset = FVector::ZeroVector;
		FVector PivotOffset = FVector::ZeroVector;

		/** 対象の速度に対してPivotが何秒分遅れているか（-dot(誤差, 速度)/|速度|^2）。等速ならPivot Dampingの値になる。 */
		float PivotLagTime() const
		{
			// ほぼ静止（1cm/s未満）では割り算が不安定なので0とする。
			const double Speed2 = Velocity.SizeSquared();
			return Speed2 > 1.0 ? static_cast<float>(-FVector::DotProduct(PivotError, Velocity) / Speed2) : 0.f;
		}

		bool IsValidForMeasure() const
		{
			return bACFMode && bOurRequestActive && bHasRig && !bBlending && Weight > FullWeightThreshold;
		}
	};

	static const UACFCameraNode_OrbitalFollow* FindOrbit(const UACFCameraPreset* Preset)
	{
		if (Preset)
		{
			for (const UACFCameraNode* Node : Preset->Nodes)
			{
				if (const UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(Node))
				{
					return Orbit;
				}
			}
		}

		return nullptr;
	}

	static const UACFCameraNode_Collision* FindCollision(const UACFCameraPreset* Preset)
	{
		if (Preset)
		{
			for (const UACFCameraNode* Node : Preset->Nodes)
			{
				if (const UACFCameraNode_Collision* Collision = Cast<UACFCameraNode_Collision>(Node))
				{
					return Collision;
				}
			}
		}

		return nullptr;
	}

	/** CSV用にベクトルを「X,Y,Z」形式へ変換します。 */
	static FString V(const FVector& In) { return FString::Printf(TEXT("%.2f,%.2f,%.2f"), In.X, In.Y, In.Z); }
#endif
}

struct UBoarGameplayCameraComponent::FTraceState
{
	FString Label;
	double StartRealTime = 0.0;
	double StartGameTime = 0.0;
	TArray<FString> Rows;
	int32 Frames = 0;
	int32 ValidFrames = 0;
	int32 CollisionHitFrames = 0;
	int32 RenderedOffFrames = 0;   // 計測可能フレームのうち、描画カメラがACF姿勢から1cm以上ずれたフレーム
	double SumDt = 0.0;
	float MinDt = TNumericLimits<float>::Max();
	float MaxDt = 0.f;
	float MaxPivotError = 0.f;
	float MaxRotError = 0.f;
	TArray<float> PivotLagSamples; // 移動中（3D速度 >= MovingSpeed）のPivot遅れ時間
	TArray<float> RotLagSamples;   // 旋回中（|Yaw速度| >= TurningYawRate）の回転遅れ時間
	TArray<float> PivotDecayTimes; // 停止後、誤差が停止時の1/e（約37%）へ減るまでの実時間
	TArray<float> RotDecayTimes;   // 旋回停止後、回転誤差が1/eへ減るまでの実時間

	// 停止の検出
	bool bWasMoving = false;
	bool bPivotDecayActive = false;
	double PivotDecayStart = 0.0;
	float PivotDecayError0 = 0.f;
	bool bWasTurning = false;
	bool bRotDecayActive = false;
	double RotDecayStart = 0.0;
	float RotDecayError0 = 0.f;
	bool bHasPrevControl = false;
	float PrevControlYaw = 0.f;
};

#if !UE_BUILD_SHIPPING
static bool BoarComputeCameraSample(const UBoarGameplayCameraComponent& Self, const APlayerController* PC, const UACFCameraSubsystem* Cameras,
	const UACFCameraPreset* Preset, FACFCameraRequestHandle Handle, BoarGameplayCamera::FSample& Out)
{
	using namespace BoarGameplayCamera;

	if (!PC)
		return false;

	Out.bACFMode = Self.IsACFCameraEnabled();
	Out.ControlRotation = PC->GetControlRotation();

	if (const APawn* Pawn = PC->GetPawn())
	{
		Out.Velocity = Pawn->GetVelocity();

		if (const UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>())
		{
			Out.SpringArmCamera = Camera->GetComponentLocation();
		}
	}

	if (const APlayerCameraManager* PCM = PC->PlayerCameraManager.Get())
	{
		Out.Rendered = PCM->GetCameraLocation();
	}

	if (!Cameras)
		return true;

	// ---- ACFの評価結果 ----
	const FACFCameraEvaluationResult Result = Cameras->GetLastEvaluationResult();
	Out.Weight = Result.Weight;
	Out.RequestWeight = Result.RequestWeight;
	Out.bBlending = Result.bIsBlending;
	Out.BlendAlpha = Result.BlendAlpha;
	Out.bOurRequestActive = Handle.IsValid() && Result.ActiveRequest == Handle;
	Out.ACFPose = Result.Pose.Location;
	Out.bHasACFPose = Result.HasInfluence();

	const UACFCameraDirector* Director = Cameras->GetDirector();
	const UACFCameraNode_OrbitalFollow* Orbit = FindOrbit(Preset);
	if (!Director || !Orbit)
		return true;

	const FACFCameraRigDebug& Debug = Director->GetLastDebug();
	if (!Debug.bHasOrbit || !Debug.bHasFollowTarget)
		return true;

	// ---- Rig Debugから誤差を計算 ----
	Out.ShoulderOffset = Orbit->ShoulderOffset;
	Out.PivotOffset = Orbit->PivotOffset;
	Out.Target = Debug.FollowTarget;
	Out.OrbitRotation = FRotator(Debug.OrbitPitch, Debug.OrbitYaw, 0.f);

	// 理想Pivot = Dampingなしの追従点（Collisionの起点。Pivot Offset込み、Lookaheadなし）
	Out.IdealPivot = Debug.bHasCollision ? Debug.CollisionOrigin : Debug.FollowTarget + Orbit->PivotOffset;

	// Damping後Pivot = Collision前のカメラ位置からアームとShoulder Offsetを戻した点（Orbit DistanceとShoulderを考慮）
	const FVector PreCollision = Debug.bHasCollision ? Debug.CollisionDesired : Result.Pose.Location;
	Out.DampedPivot = PreCollision
		+ Out.OrbitRotation.Vector() * Debug.OrbitDistance
		- Out.OrbitRotation.RotateVector(Orbit->ShoulderOffset);

	Out.PivotError = Out.DampedPivot - Out.IdealPivot;
	Out.PivotErrorCamera = FRotator(0.f, Debug.OrbitYaw, 0.f).UnrotateVector(Out.PivotError);
	Out.RotErrorYaw = FRotator::NormalizeAxis(Out.ControlRotation.Yaw - Debug.OrbitYaw);
	Out.RotErrorPitch = FRotator::NormalizeAxis(Out.ControlRotation.Pitch) - Debug.OrbitPitch;
	Out.bCollisionHit = Debug.bHasCollision && Debug.bCollisionHit;

	if (Debug.bHasCollision)
	{
		Out.DesiredDistance = static_cast<float>(FVector::Dist(Debug.CollisionOrigin, Debug.CollisionDesired));
		Out.ActualDistance = static_cast<float>(FVector::Dist(Debug.CollisionOrigin, Result.Pose.Location));
	}

	Out.bHasRig = true;
	return true;
}
#endif

void UBoarGameplayCameraComponent::DumpCameraState() const
{
	const APlayerController* PC = GetOwningController();
	const UACFCameraSubsystem* Cameras = GetCameraSubsystem();

	UE_LOG(LogBoarCamera, Display,
		TEXT("[Camera] %s モード=%s（CVar Boar.Camera.ACF=%d、既定値=%d）Preset=%s Requested=%s Request=%s Alive=%d Active=%d"),
		*GetNameSafe(PC), IsACFCameraEnabled() ? TEXT("ACF") : TEXT("SpringArm"), BoarGameplayCamera::ACFOverride,
		bUseACFCameraByDefault ? 1 : 0, *GetNameSafe(NormalCameraPreset), *GetNameSafe(RequestedPreset), *RequestHandle.ToString(),
		Cameras && Cameras->IsCameraRequestAlive(RequestHandle) ? 1 : 0,
		Cameras && Cameras->IsActiveCamera(RequestHandle) ? 1 : 0);

	if (Cameras)
	{
		const FACFCameraEvaluationResult Result = Cameras->GetLastEvaluationResult();
		UE_LOG(LogBoarCamera, Display,
			TEXT("[Camera]   ACF Weight=%.3f RequestWeight=%.3f Blending=%d Suppressed=%d Bypassed=%d ActiveRequest=%s NumRequests=%d Pose Loc=%s Rot=%s FOV=%.2f"),
			Result.Weight, Result.RequestWeight, Result.bIsBlending ? 1 : 0, Result.bOutputSuppressed ? 1 : 0, Result.bOutputBypassed ? 1 : 0,
			*Result.ActiveRequest.ToString(), Result.NumRequests,
			*Result.Pose.Location.ToCompactString(), *Result.Pose.Rotation.ToCompactString(), Result.Pose.FieldOfView);

		// Requestがあることと、画面のカメラがACFの結果であることは別。Output Modifierの書き込み回数と、描画カメラとACF姿勢の差で判定する。
		const FACFCameraOutputDiagnostics Output = Cameras->GetOutputDiagnostics();
		const APlayerCameraManager* PCM = PC ? PC->PlayerCameraManager.Get() : nullptr;
		const float ToACF = PCM
			? static_cast<float>(FVector::Dist(PCM->GetCameraLocation(), Result.Pose.Location))
			: -1.f;

		const float ToACFDeg = PCM
			? static_cast<float>(FMath::RadiansToDegrees(PCM->GetCameraRotation().Quaternion().AngularDistance(Result.Pose.Rotation.Quaternion())))
			: -1.f;

		const TCHAR* Authority = nullptr;

		if (!Result.HasInfluence() || Output.Writes == 0)
		{
			Authority = TEXT("ACFではない（SpringArm／別のView Target）");
		}
		else if (Result.Weight < BoarGameplayCamera::FullWeightThreshold)
		{
			Authority = TEXT("ブレンド中（ACF＋上流のカメラ）");
		}
		else if (ToACF <= BoarGameplayCamera::RenderedMatchDistance && ToACFDeg <= BoarGameplayCamera::RenderedMatchAngleDeg)
		{
			Authority = TEXT("ACF");
		}
		else
		{
			Authority = TEXT("ACFは書き込んだが描画カメラと一致しない（Shake／Collision Guard／後段のModifier？）");
		}

		UE_LOG(LogBoarCamera, Display,
			TEXT("[Camera]   出力: Modifier passes=%d writes=%d unresolved=%d（frame %lld、現在 %llu）描画-ACF差 %.2fcm %.2fdeg -> 画面の主導=%s"),
			Output.Passes, Output.Writes, Output.UnresolvedPasses, Output.Frame, static_cast<uint64>(GFrameCounter), ToACF, ToACFDeg, Authority);
	}

#if !UE_BUILD_SHIPPING
	// アセット（DA）に保存されている値。下の「使用中」と違えば検証用CVar／Runtime Tuningが効いている。
	if (const UACFCameraNode_OrbitalFollow* AssetOrbit = BoarGameplayCamera::FindOrbit(NormalCameraPreset))
	{
		const UACFCameraNode_Collision* AssetCollision = BoarGameplayCamera::FindCollision(NormalCameraPreset);
		UE_LOG(LogBoarCamera, Display,
			TEXT("[Camera]   アセット %s のDamping: Pivot=%s (%s) Rotation=%s CollisionPullIn=%.3f CollisionReturn=%.3f BlendIn=%.2f BlendOut=%.2f"),
			*GetNameSafe(NormalCameraPreset), *AssetOrbit->PivotDamping.Time.ToCompactString(),
			BoarGameplayCamera::DampingSpaceName(AssetOrbit->PivotDamping.Space),
			*AssetOrbit->RotationDamping.ToString(),
			AssetCollision ? AssetCollision->PullInDampingTime : -1.f,
			AssetCollision ? AssetCollision->ReturnDampingTime : -1.f,
			NormalCameraPreset->DefaultBlendIn.Duration,
			NormalCameraPreset->DefaultBlendOut.Duration);
	}

	// 実際に使われているDamping値（Runtime Tuning中はその値、検証用複製ならその値）
	if (const UACFCameraNode_OrbitalFollow* Orbit = BoarGameplayCamera::FindOrbit(RequestedPreset))
	{
		FVector PivotDamping = Orbit->PivotDamping.Time;
		FACFCameraTuningValue Tuned;

		if (PC && UACFCameraTuningLibrary::GetCameraTuningValue(const_cast<APlayerController*>(PC), RequestHandle,
			EACFCameraTuningParameter::FollowDamping, Tuned) == EACFCameraTuningResult::Ok)
		{
			PivotDamping = Tuned.Value;
		}

		const UACFCameraNode_Collision* Collision = BoarGameplayCamera::FindCollision(RequestedPreset);
		UE_LOG(LogBoarCamera, Display,
			TEXT("[Camera]   使用中のDamping: Pivot=%s Rotation=%s CollisionPullIn=%.3f CollisionReturn=%.3f（検証CVar Pivot=%.3f Rot=%.3f Return=%.3f、負 = アセット値）"),
			*PivotDamping.ToCompactString(), *Orbit->RotationDamping.ToString(),
			Collision ? Collision->PullInDampingTime : -1.f,
			Collision ? Collision->ReturnDampingTime : -1.f,
			BoarGameplayCamera::TestPivotDamping, BoarGameplayCamera::TestRotationDamping, BoarGameplayCamera::TestReturnDamping);
	}

	BoarGameplayCamera::FSample S;

	if (BoarComputeCameraSample(*this, PC, Cameras, RequestedPreset, RequestHandle, S) && S.bHasRig)
	{
		UE_LOG(LogBoarCamera, Display,
			TEXT("[Camera]   Target=%s IdealPivot=%s DampedPivot=%s PivotErr=%.2fcm（前/右/上 %s）Speed=%.1f LagTime=%.3fs"),
			*S.Target.ToCompactString(), *S.IdealPivot.ToCompactString(), *S.DampedPivot.ToCompactString(), S.PivotError.Size(),
			*S.PivotErrorCamera.ToCompactString(), S.Velocity.Size(), S.PivotLagTime());

		UE_LOG(LogBoarCamera, Display,
			TEXT("[Camera]   ControlRot=%s OrbitRot(Damping後)=%s RotErr Yaw=%.3f Pitch=%.3f Collision Hit=%d Desired=%.1f Actual=%.1f"),
			*S.ControlRotation.ToCompactString(), *S.OrbitRotation.ToCompactString(), S.RotErrorYaw, S.RotErrorPitch,
			S.bCollisionHit ? 1 : 0, S.DesiredDistance, S.ActualDistance);
	}
#endif

	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		if (const UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>())
		{
			UE_LOG(LogBoarCamera, Display, TEXT("[Camera]   SpringArmのFollowCamera Loc=%s Rot=%s FOV=%.2f ControlRot=%s"),
				*Camera->GetComponentLocation().ToCompactString(), *Camera->GetComponentRotation().ToCompactString(),
				Camera->FieldOfView, *PC->GetControlRotation().ToCompactString());
		}

		if (const USpringArmComponent* Arm = Pawn->FindComponentByClass<USpringArmComponent>())
		{
			// 比較用（Boar.Camera.ACF 0）。Lag Speed s はおおよそ 1/s 秒のDampingに相当。
			UE_LOG(LogBoarCamera, Display,
				TEXT("[Camera]   SpringArm Lag（ACF 0 比較用）: Pos=%d Speed=%.1f (~%.3fs) Rot=%d Speed=%.1f (~%.3fs) Arm=%.0f Socket=%s"),
				Arm->bEnableCameraLag ? 1 : 0, Arm->CameraLagSpeed,
				Arm->CameraLagSpeed > 0.f ? 1.f / Arm->CameraLagSpeed : 0.f,
				Arm->bEnableCameraRotationLag ? 1 : 0, Arm->CameraRotationLagSpeed,
				Arm->CameraRotationLagSpeed > 0.f ? 1.f / Arm->CameraRotationLagSpeed : 0.f,
				Arm->TargetArmLength,
				*Arm->SocketOffset.ToCompactString());
		}
	}

	if (const APlayerCameraManager* PCM = PC ? PC->PlayerCameraManager.Get() : nullptr)
	{
		UE_LOG(LogBoarCamera, Display, TEXT("[Camera]   描画カメラ Loc=%s Rot=%s FOV=%.2f ViewTarget=%s"),
			*PCM->GetCameraLocation().ToCompactString(), *PCM->GetCameraRotation().ToCompactString(), PCM->GetFOVAngle(),
			*GetNameSafe(PCM->GetViewTarget()));
	}
}

void UBoarGameplayCameraComponent::StartTrace(const FString& Label)
{
#if !UE_BUILD_SHIPPING
	if (Trace)
	{
		StopTrace();
	}

	Trace = MakeShared<FTraceState>();
	Trace->Label = FPaths::MakeValidFileName(Label);
	Trace->StartRealTime = FPlatformTime::Seconds();
	Trace->StartGameTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Trace->Rows.Reserve(BoarGameplayCamera::TraceRowReserve);

	// CSVのヘッダー行。列の並びは HandleWorldPostActorTick で追加する行と揃えること。
	Trace->Rows.Add(TEXT("RealTime,GameTime,Dt,Mode,OurRequestActive,Weight,RequestWeight,Blending,BlendAlpha,")
		TEXT("TargetX,TargetY,TargetZ,VelX,VelY,VelZ,Speed,IdealPivotX,IdealPivotY,IdealPivotZ,DampedPivotX,DampedPivotY,DampedPivotZ,")
		TEXT("PivotErr,PivotErrFwd,PivotErrRight,PivotErrUp,PivotLagTime,CtrlPitch,CtrlYaw,OrbitPitch,OrbitYaw,RotErrYaw,RotErrPitch,CtrlYawRate,RotLagTime,")
		TEXT("CollisionHit,DesiredDist,ActualDist,RenderedX,RenderedY,RenderedZ,SpringArmX,SpringArmY,SpringArmZ,ValidForMeasure,RenderedToACF"));

	FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
	FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::HandleWorldPostActorTick);

	UE_LOG(LogBoarCamera, Display, TEXT("[CameraTrace] 記録開始 '%s'"), *Trace->Label);
#endif
}

void UBoarGameplayCameraComponent::HandleWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
	// カメラ更新の後（UE 5.8ではOnWorldPostActorTickはPCMの更新より後）に、そのフレームの結果を記録する。
	if (!Trace || World != GetWorld())
		return;

	using namespace BoarGameplayCamera;

	FTraceState& T = *Trace;
	FSample S;
	BoarComputeCameraSample(*this, GetOwningController(), GetCameraSubsystem(), RequestedPreset, RequestHandle, S);

	const double Now = FPlatformTime::Seconds() - T.StartRealTime;
	const double GameTime = World->GetTimeSeconds() - T.StartGameTime;
	const float Dt = DeltaSeconds;

	float YawRate = 0.f;

	if (T.bHasPrevControl && Dt > 0.f)
	{
		YawRate = FRotator::NormalizeAxis(S.ControlRotation.Yaw - T.PrevControlYaw) / Dt;
	}

	T.PrevControlYaw = S.ControlRotation.Yaw;
	T.bHasPrevControl = true;

	const bool bValid = S.IsValidForMeasure();
	const float Speed = S.Velocity.Size();
	const float PivotErr = S.PivotError.Size();
	const float RotErr = FMath::Max(FMath::Abs(S.RotErrorYaw), FMath::Abs(S.RotErrorPitch));
	const bool bMoving = Speed >= MovingSpeed;
	const bool bTurning = FMath::Abs(YawRate) >= TurningYawRate;
	const float PivotLag = bValid && bMoving ? S.PivotLagTime() : 0.f;
	const float RotLag = bValid && bTurning ? S.RotErrorYaw / YawRate : 0.f;

	// 描画カメラとACF姿勢の距離（-1 = ACFの出力なし）。CameraShake・Collision Guard中は0にならない。
	const float RenderedToACF = S.bHasACFPose ? static_cast<float>(FVector::Dist(S.Rendered, S.ACFPose)) : -1.f;

	++T.Frames;
	T.SumDt += Dt;
	T.MinDt = FMath::Min(T.MinDt, Dt);
	T.MaxDt = FMath::Max(T.MaxDt, Dt);

	if (bValid)
	{
		++T.ValidFrames;
		T.CollisionHitFrames += S.bCollisionHit ? 1 : 0;
		T.RenderedOffFrames += RenderedToACF > RenderedMatchDistance ? 1 : 0;
		T.MaxPivotError = FMath::Max(T.MaxPivotError, PivotErr);
		T.MaxRotError = FMath::Max(T.MaxRotError, RotErr);

		if (bMoving)
		{
			T.PivotLagSamples.Add(PivotLag);
		}

		if (bTurning)
		{
			T.RotLagSamples.Add(RotLag);
		}

		// 停止後の収束: 移動（>= DecayMovingSpeed）から停止（< DecayStoppedSpeed）した時点の誤差が1/eになるまでの実時間
		if (Speed >= DecayMovingSpeed)
		{
			T.bWasMoving = true;
			T.bPivotDecayActive = false;
		}
		else if (T.bWasMoving && Speed < DecayStoppedSpeed)
		{
			T.bWasMoving = false;

			if (PivotErr >= DecayMinPivotError)
			{
				T.bPivotDecayActive = true;
				T.PivotDecayStart = Now;
				T.PivotDecayError0 = PivotErr;
			}
		}

		if (T.bPivotDecayActive)
		{
			if (Speed >= DecayAbortSpeed)
			{
				T.bPivotDecayActive = false;
			}
			else if (PivotErr <= T.PivotDecayError0 * InverseE)
			{
				T.PivotDecayTimes.Add(static_cast<float>(Now - T.PivotDecayStart));
				T.bPivotDecayActive = false;
			}
		}

		// 旋回停止後の収束: 旋回（>= TurningYawRate）から停止（< DecayStoppedYawRate）した時点の回転誤差が1/eになるまでの実時間
		if (bTurning)
		{
			T.bWasTurning = true;
			T.bRotDecayActive = false;
		}
		else if (T.bWasTurning && FMath::Abs(YawRate) < DecayStoppedYawRate)
		{
			T.bWasTurning = false;

			if (RotErr >= DecayMinRotError)
			{
				T.bRotDecayActive = true;
				T.RotDecayStart = Now;
				T.RotDecayError0 = RotErr;
			}
		}

		if (T.bRotDecayActive)
		{
			if (FMath::Abs(YawRate) >= DecayAbortYawRate)
			{
				T.bRotDecayActive = false;
			}
			else if (RotErr <= T.RotDecayError0 * InverseE)
			{
				T.RotDecayTimes.Add(static_cast<float>(Now - T.RotDecayStart));
				T.bRotDecayActive = false;
			}
		}
	}

	// ヘッダー行と同じ列順で1行追加する。
	T.Rows.Add(FString::Printf(
		TEXT("%.4f,%.4f,%.5f,%s,%d,%.3f,%.3f,%d,%.3f,%s,%s,%.2f,%s,%s,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.4f,%d,%.2f,%.2f,%s,%s,%d,%.2f"),
		Now, GameTime, Dt, S.bACFMode ? TEXT("ACF") : TEXT("SpringArm"), S.bOurRequestActive ? 1 : 0, S.Weight, S.RequestWeight,
		S.bBlending ? 1 : 0, S.BlendAlpha,
		*V(S.Target), *V(S.Velocity), Speed, *V(S.IdealPivot), *V(S.DampedPivot),
		PivotErr, S.PivotErrorCamera.X, S.PivotErrorCamera.Y, S.PivotErrorCamera.Z, PivotLag,
		S.ControlRotation.Pitch, S.ControlRotation.Yaw, S.OrbitRotation.Pitch, S.OrbitRotation.Yaw,
		S.RotErrorYaw, S.RotErrorPitch, YawRate, RotLag,
		S.bCollisionHit ? 1 : 0, S.DesiredDistance, S.ActualDistance,
		*V(S.Rendered), *V(S.SpringArmCamera), bValid ? 1 : 0, RenderedToACF));

	if (GEngine)
	{
		// Component毎に固定のKeyを使い、画面上の表示を毎フレーム上書きする。
		const uint64 MessageKey = static_cast<uint64>(reinterpret_cast<UPTRINT>(this));
		GEngine->AddOnScreenDebugMessage(MessageKey, 0.f, bValid ? FColor::Cyan : FColor::Orange,
			FString::Printf(
				TEXT("[CameraTrace %s] t=%.2fs dt=%.1fms W=%.2f%s | PivotErr %.1fcm lag %.3fs (speed %.0f) | RotErr yaw %.2f pitch %.2f deg | Col %s %.0f/%.0fcm"),
				*T.Label, Now, Dt * 1000.f, S.Weight, S.bBlending ? TEXT(" BLEND") : TEXT(""), PivotErr, PivotLag, Speed,
				S.RotErrorYaw, S.RotErrorPitch, S.bCollisionHit ? TEXT("HIT") : TEXT("-"), S.ActualDistance, S.DesiredDistance));
	}
#endif
}

void UBoarGameplayCameraComponent::StopTrace()
{
#if !UE_BUILD_SHIPPING
	FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);

	if (!Trace)
		return;

	const TSharedPtr<FTraceState> Done = MoveTemp(Trace);
	Trace.Reset();

	auto Median = [](TArray<float> Values) -> float
	{
		if (Values.IsEmpty())
			return 0.f;

		Values.Sort();
		return Values[Values.Num() / 2];
	};

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("BoarCameraTrace");
	const FString File = Dir / FString::Printf(TEXT("%s_%s.csv"), *Done->Label, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	const bool bSaved = FFileHelper::SaveStringArrayToFile(Done->Rows, *File);
	const float AvgDt = Done->Frames > 0 ? static_cast<float>(Done->SumDt / Done->Frames) : 0.f;

	UE_LOG(LogBoarCamera, Display,
		TEXT("[CameraTrace] 記録停止 '%s': %dフレーム（計測可能 %d）、平均dt %.2fms (%.1f fps)、最小 %.2fms、最大 %.2fms。CSV %s: %s"),
		*Done->Label, Done->Frames, Done->ValidFrames, AvgDt * 1000.f, AvgDt > 0.f ? 1.f / AvgDt : 0.f,
		Done->MinDt * 1000.f, Done->MaxDt * 1000.f,
		bSaved ? TEXT("保存済み") : TEXT("保存失敗"), *FPaths::ConvertRelativePathToFull(File));

	UE_LOG(LogBoarCamera, Display,
		TEXT("[CameraTrace]   Pivot: 移動中の遅れ時間 中央値 %.3fs（%dフレーム）、停止後に1/eへ収束 中央値 %.3fs（停止 %d回）、最大誤差 %.1fcm"),
		Median(Done->PivotLagSamples), Done->PivotLagSamples.Num(),
		Median(Done->PivotDecayTimes), Done->PivotDecayTimes.Num(), Done->MaxPivotError);

	UE_LOG(LogBoarCamera, Display,
		TEXT("[CameraTrace]   回転: 旋回中の遅れ時間 中央値 %.3fs（%dフレーム）、旋回停止後に1/eへ収束 中央値 %.3fs（%d回）、最大誤差 %.2fdeg"),
		Median(Done->RotLagSamples), Done->RotLagSamples.Num(),
		Median(Done->RotDecayTimes), Done->RotDecayTimes.Num(), Done->MaxRotError);

	UE_LOG(LogBoarCamera, Display,
		TEXT("[CameraTrace]   Collision Hit %dフレーム。描画カメラがACF姿勢から1cm超ずれたのは計測可能 %d フレーム中 %d（0 = 画面はACFの結果。Shake／Collision Guardも含む）"),
		Done->CollisionHitFrames, Done->ValidFrames, Done->RenderedOffFrames);
#endif
}
