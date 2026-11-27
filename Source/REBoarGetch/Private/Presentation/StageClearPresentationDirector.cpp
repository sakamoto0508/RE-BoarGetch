#include "Presentation/StageClearPresentationDirector.h"
#include "REBoarGetch.h"
#include "UI/BoarUILayers.h"

#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Player/BoarPlayerCharacter.h"
#include "Player/BoarPlayerController.h"
#include "UI/BoarStageClearBannerWidget.h"

namespace StageClearPresentation
{
	/** CapsuleのHalf Heightが取れないときの既定値（cm）。 */
	constexpr float DefaultCapsuleHalfHeight = 90.f;

	// ---- 勝利カメラの向きの選び方 ----

	/** 基本の左右の向きで塞がっていたとき、さらに外側へ振る角度（度）。 */
	constexpr float CameraFallbackYawStep = 45.f;

	/** カメラの最終位置までのSweep球と、現在のカメラから開始位置までのSweep球の半径（cm）。 */
	constexpr float CameraProbeRadius = 15.f;
	constexpr float BlendPathProbeRadius = 10.f;

	/** 現在のカメラから開始位置までが通る向きを優先するための加点と、より良いとみなす最小の差。 */
	constexpr float ClearBlendPathBonus = .5f;
	constexpr float BetterScoreMargin = .05f;

	/** 壁の手前で止めるとき、当たった位置からさらに手前に戻す距離（cm）。 */
	constexpr float RetreatWallMargin = 20.f;

	// ---- アニメーション ----

	/** Montageが見た目に反映されているかを確認するまで待つフレーム数（Slotの重みは1フレーム遅れて入る）。 */
	constexpr int32 MontageCheckFrame = 2;

	/** 勝利Montageを止める位置: 終端からBlend Out＋この秒数手前（最低でもこの秒数手前）。 */
	constexpr float MontageHoldMargin = .05f;

	/** 演出終了時に勝利Montageを止めるBlend時間（秒）。 */
	constexpr float StopMontageBlend = .1f;

	/** 代替ジャンプの滞空時間（秒）。 */
	constexpr float FallbackJumpDuration = .55f;

	// ---- VFX ----

	/** 網が見つからないとき、星を出す高さ（足元からcm）。 */
	constexpr float StarFallbackHeight = 110.f;

	/** 星のVFXを色ごとに回す角度（3色で一周）。 */
	constexpr float StarYawStep = 120.f;

	/** Meshが無いときに分解範囲として使う箱（足元からの半径と下端・上端、cm）。 */
	const FVector DissolveFallbackMin(40, 40, 70);
	const FVector DissolveFallbackMax(40, 40, 110);

	/** 転送VFXを足元から少し浮かせる高さ（cm）、Scale、不透明度。 */
	constexpr float TransferVFXLift = 2.f;
	constexpr float TransferVFXScale = .7f;
	constexpr float TransferVFXAlpha = .85f;

	/** 発光の開始時の明るさと、分解の最後に1を少し超えて完全に消すためのDissolveProgressの終点。 */
	constexpr float GlowStartAmount = .1f;
	constexpr float DissolveProgressEnd = 1.02f;

	/** 分解開始時にDataMaterialの発光へ足す量（終了時には0）。 */
	constexpr float DissolveGlowExtra = .5f;

	/** 0除算・極端に短い演出を避けるための各時間の下限（秒）。 */
	constexpr float MinRampDuration = .01f;
	constexpr float MinDissolveDuration = .05f;
	constexpr float MinRetreatDuration = .1f;
	constexpr float MinParticleFadeDuration = .05f;
	constexpr float MinScreenFadeDuration = .01f;

}

AStageClearPresentationDirector::AStageClearPresentationDirector()
{
	// 金・白・シアンの3色の星。
	StarColors = {FLinearColor(1.f, .82f, .08f), FLinearColor(1.f, 1.f, 1.f), FLinearColor(.05f, .85f, 1.f)};
}

void AStageClearPresentationDirector::BeginPlay()
{
	Super::BeginPlay();

	// 必須の演出アセットが未設定なら1回だけErrorを出す。欠けた演出は個別に省き、Clear進行とResult表示は継続する。
	TArray<FString> Missing;

	if (!StarVFX)
		Missing.Add(TEXT("StarVFX"));

	if (!GlowMaterial)
		Missing.Add(TEXT("GlowMaterial"));

	if (!DataMaterial)
		Missing.Add(TEXT("DataMaterial"));

	if (!TransferVFX)
		Missing.Add(TEXT("TransferVFX"));

	if (!DataFragmentVFX)
		Missing.Add(TEXT("DataFragmentVFX (NS_StageClearDataFragments)"));

	if (!Missing.IsEmpty())
	{
		UE_LOG(LogREBoarGetch, Error, TEXT("[StageClear] %s: %s が未設定です。該当する演出を省きます。"), *GetName(),
			*FString::Join(Missing, TEXT(", ")));
	}
}

void AStageClearPresentationDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	RestoreVisuals();
	RestoreCamera();
	Super::EndPlay(Reason);
}

void AStageClearPresentationDirector::CollectMeshes()
{
	HiddenMeshes.Reset();

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player)
		return;

	// 網などの装備はMesh Socketへ子Actorとしてアタッチされています。孫Actorまで辿り、網だけ残らないようにします。
	TArray<AActor*> Actors = {Player};
	for (int32 I = 0; I < Actors.Num(); ++I)
	{
		TArray<AActor*> Attached;
		Actors[I]->GetAttachedActors(Attached, false);

		for (AActor* A : Attached)
		{
			Actors.AddUnique(A);
		}
	}

	for (AActor* A : Actors)
	{
		TArray<UMeshComponent*> Meshes;
		A->GetComponents<UMeshComponent>(Meshes);

		for (UMeshComponent* Mesh : Meshes)
		{
			const bool bSupportedMesh = Cast<USkeletalMeshComponent>(Mesh) || Cast<UStaticMeshComponent>(Mesh);
			if (!Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame || !bSupportedMesh)
				continue;

			FStageClearHiddenMesh Entry;
			Entry.Source = Mesh;
			Entry.bOriginalVisible = Mesh->IsVisible();
			Entry.bOriginalHiddenInGame = Mesh->bHiddenInGame;
			Entry.OriginalOverlay = Mesh->GetOverlayMaterial();
			HiddenMeshes.Add(Entry);
		}
	}
}

void AStageClearPresentationDirector::BeginVictoryShot()
{
	using namespace StageClearPresentation;

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	APlayerController* PC = GetPresentationController();
	if (bVictoryShot || !Player || !PC)
		return;

	bVictoryShot = true;
	SetPlayerHUDHidden(true);
	CollectMeshes();
	MeshOriginalRelativeLocation = Player->GetMesh()->GetRelativeLocation();

	// 消失地点は開始時に固定し、以後プレイヤーやボーンの動きに追従させません。
	const float HalfHeight = Player->GetCapsuleComponent()
		? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: DefaultCapsuleHalfHeight;

	VictoryFocusPoint = Player->GetActorLocation() - FVector(0, 0, HalfHeight) + VictoryFocusOffset;

	// ---- 遮蔽物の少ない向きを選び、カメラが壁へめり込まないようにします ----
	FCollisionQueryParams Params(SCENE_QUERY_STAT(StageClearCamera), false, Player);
	TArray<AActor*> Ignored;
	Player->GetAttachedActors(Ignored, true);
	Params.AddIgnoredActors(Ignored);

	const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();

	// 斜め前の片側→反対側→それぞれさらに外側→真正面の順に試す。
	const float Candidates[] = {
		CameraYawOffset,
		-CameraYawOffset,
		CameraYawOffset + CameraFallbackYawStep,
		-CameraYawOffset - CameraFallbackYawStep,
		0.f
	};

	float BestFraction = -1.f;
	float BestScore = -1.f;
	const FVector CurrentCamera = PC->PlayerCameraManager
		? PC->PlayerCameraManager->GetCameraLocation()
		: VictoryFocusPoint;

	for (float Yaw : Candidates)
	{
		const FVector Dir = Forward.RotateAngleAxis(Yaw, FVector::UpVector);
		const FVector Target = VictoryFocusPoint
			+ Dir * (CameraDistance + RetreatDistance)
			+ FVector(0, 0, CameraHeight + RetreatHeight);

		FHitResult Hit;
		const bool bHit = GetWorld()->SweepSingleByChannel(Hit, VictoryFocusPoint, Target, FQuat::Identity, ECC_Camera,
			FCollisionShape::MakeSphere(CameraProbeRadius), Params);
		const float Fraction = bHit ? Hit.Time : 1.f;

		// Blendは直線補間なので、現在のカメラから開始位置までが通るかも評価します（檻などを突き抜けないように）。
		const FVector Start = VictoryFocusPoint + Dir * CameraDistance + FVector(0, 0, CameraHeight);
		const bool bBlendBlocked = GetWorld()->SweepTestByChannel(CurrentCamera, Start, FQuat::Identity, ECC_Camera,
			FCollisionShape::MakeSphere(BlendPathProbeRadius), Params);

		const float Score = Fraction + (bBlendBlocked ? 0.f : ClearBlendPathBonus);
		if (Score > BestScore + BetterScoreMargin)
		{
			BestScore = Score;
			BestFraction = Fraction;
			CameraDirection = Dir;
		}

		// 完全に通る向きが見つかったら、それ以上は探さない。
		if (!bHit && !bBlendBlocked)
			break;
	}

	// 最終位置まで通らない場合は、通れる距離だけ後退します（近距離側は維持）。
	const float Full = FVector(CameraDistance + RetreatDistance, 0, CameraHeight + RetreatHeight).Size();
	const float Near = FVector(CameraDistance, 0, CameraHeight).Size();
	const float Allowed = FMath::Max(Near, Full * BestFraction - RetreatWallMargin);
	const float Scale = Full > Near ? FMath::Clamp((Allowed - Near) / (Full - Near), 0.f, 1.f) : 1.f;
	MaxRetreatDistance = RetreatDistance * Scale;
	MaxRetreatHeight = RetreatHeight * Scale;

	// ---- 勝利カメラを生成して切り替える ----
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector CameraStart = ComputeCameraLocation(0.f);
	VictoryCamera = GetWorld()->SpawnActor<ACameraActor>(CameraStart, (VictoryFocusPoint - CameraStart).Rotation(), SpawnParams);
	if (VictoryCamera)
	{
		VictoryCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
		VictoryCamera->GetCameraComponent()->SetFieldOfView(VictoryFOV);
		OriginalViewTarget = PC->GetViewTarget();

		// ACFはViewTargetに関係なく上に乗るため、勝利カメラを外部ViewTargetとして登録し、ACFの通常カメラで上書きさせない。
		// 切替・復帰のブレンドは従来どおりPCMのViewTarget Blendだけが行う（ACFは裏で評価を続ける）。
		if (UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(PC))
		{
			Cameras->AddExternalViewTarget(VictoryCamera, this);
		}

		PC->SetViewTargetWithBlend(VictoryCamera, CameraBlendTime, VTBlend_Cubic);
	}

	UE_LOG(LogREBoarGetch, Log, TEXT("[StageClear] 勝利演出開始 Focus=%s RetreatScale=%.2f"), *VictoryFocusPoint.ToString(), Scale);
}

FVector AStageClearPresentationDirector::ComputeCameraLocation(float A) const
{
	return VictoryFocusPoint
		+ CameraDirection * (CameraDistance + MaxRetreatDistance * A)
		+ FVector(0, 0, CameraHeight + MaxRetreatHeight * A);
}

float AStageClearPresentationDirector::EvaluateRetreat(float T) const
{
	T = FMath::Clamp(T, 0.f, 1.f);

	if (RetreatCurve)
		return FMath::Clamp(RetreatCurve->GetFloatValue(T), 0.f, 1.f);

	return FMath::InterpEaseInOut(0.f, 1.f, T, RetreatEaseExponent);
}

void AStageClearPresentationDirector::UpdateCamera(float A)
{
	if (!VictoryCamera)
		return;

	const FVector Location = ComputeCameraLocation(A);
	VictoryCamera->SetActorLocationAndRotation(Location, (VictoryFocusPoint - Location).Rotation());
	VictoryCamera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(VictoryFOV, RetreatEndFOV, A));
}

void AStageClearPresentationDirector::PlayVictoryMontage()
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player || bMontagePlaying || bFallbackJump)
		return;

	MontageAge = 0.f;
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	const float Length = (Anim && VictoryMontage) ? Anim->Montage_Play(VictoryMontage, MontagePlayRate) : 0.f;
	bMontagePlaying = Length > 0.f;
	bFallbackJump = !bMontagePlaying;

	ShowBanner();

	if (bFallbackJump)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("[StageClear] 勝利Montageが使えないため、代替のジャンプを使います。"));
	}
}

void AStageClearPresentationDirector::UpdateMontage()
{
	using namespace StageClearPresentation;

	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player)
		return;

	if (bMontagePlaying && !bSingleNodePlayback && bAllowSingleNodeFallback && ++MontageCheckFrames == MontageCheckFrame)
	{
		// Slotが現在のAnimGraph経路に無いとMontageは見た目に反映されません。その場合は同じアニメをSingle Nodeで再生します。
		UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
		const FName Slot = VictoryMontage->SlotAnimTracks.IsEmpty()
			? NAME_None
			: VictoryMontage->SlotAnimTracks[0].SlotName;

		UAnimSequenceBase* Sequence = VictoryAnimation.Get();
		if (!Sequence && !VictoryMontage->SlotAnimTracks.IsEmpty()
			&& !VictoryMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
		{
			Sequence = VictoryMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
		}

		if (Anim && Sequence && Anim->GetSlotNodeGlobalWeight(Slot) <= KINDA_SMALL_NUMBER)
		{
			const float Position = Anim->Montage_GetPosition(VictoryMontage);
			Anim->Montage_Stop(0.f, VictoryMontage);
			Player->GetMesh()->PlayAnimation(Sequence, false);
			Player->GetMesh()->SetPosition(Position, false);
			Player->GetMesh()->SetPlayRate(MontagePlayRate);
			bSingleNodePlayback = true;
			bMontageHeld = true; // 非ループのSingle Nodeは最終フレームで停止し、掲げた姿勢を保持します。

			UE_LOG(LogREBoarGetch, Log,
				TEXT("[StageClear] Slot '%s' がAnimGraphで有効ではないため、%s を単体ノードとして再生します。"),
				*Slot.ToString(), *GetNameSafe(Sequence));
		}
	}

	if (bMontagePlaying && !bMontageHeld)
	{
		// 最終ポーズ（網を掲げた姿勢）で止め、消失が終わるまで通常AnimBPへ戻しません。
		UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
		const float HoldAt = VictoryMontage->GetPlayLength()
			- FMath::Max(MontageHoldMargin, VictoryMontage->BlendOut.GetBlendTime() + MontageHoldMargin);

		if (Anim && Anim->Montage_IsPlaying(VictoryMontage) && Anim->Montage_GetPosition(VictoryMontage) >= HoldAt)
		{
			Anim->Montage_Pause(VictoryMontage);
			bMontageHeld = true;
		}
	}

	if (bFallbackJump)
	{
		// Montageが無いときは、Meshを上下させるだけの小さなジャンプで代用する。
		const float JumpProgress = FMath::Clamp((MontageAge - FallbackJumpDelay) / FallbackJumpDuration, 0.f, 1.f);
		const float H = FallbackJumpHeight * FMath::Sin(PI * JumpProgress);
		Player->GetMesh()->SetRelativeLocation(MeshOriginalRelativeLocation + FVector(0, 0, H));
	}
}

void AStageClearPresentationDirector::SpawnStarBurst()
{
	using namespace StageClearPresentation;

	if (!StarVFX)
		return;

	// 網Meshの上端（掲げた先端）から放ちます。網が無い場合は頭上を使います。
	FVector Origin = VictoryFocusPoint + FVector(0, 0, StarFallbackHeight);

	if (ABoarPlayerCharacter* Player = GetPresentationPlayer())
	{
		float BestZ = -BIG_NUMBER;

		for (const FStageClearHiddenMesh& M : HiddenMeshes)
		{
			// Player本体以外（装備）のMeshのうち、一番高い上端を探す。
			if (!M.Source.IsValid() || M.Source->GetOwner() == Player)
				continue;

			const FBoxSphereBounds B = M.Source->Bounds;
			if (B.Origin.Z + B.BoxExtent.Z > BestZ)
			{
				BestZ = B.Origin.Z + B.BoxExtent.Z;
				Origin = FVector(B.Origin.X, B.Origin.Y, BestZ);
			}
		}
	}

	Origin += StarOffset;

	for (int32 I = 0; I < StarColors.Num(); ++I)
	{
		UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, StarVFX, Origin, FRotator(0, StarYawStep * I, 0), FVector(StarScale), true, true, ENCPoolMethod::None, true);
		if (!C)
			continue;

		C->SetVariableLinearColor(TEXT("User.Color"), StarColors[I]);
		C->SetVariableFloat(TEXT("User.Seed"), float(I));
		StarComponents.Add(C);
	}
}

void AStageClearPresentationDirector::BeginGlow(float RampDuration)
{
	using namespace StageClearPresentation;

	if (bGlowing || !GlowMaterial)
		return;

	bGlowing = true;
	GlowAge = 0.f;
	GlowRamp = FMath::Max(MinRampDuration, RampDuration);

	GlowMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
	GlowMID->SetVectorParameterValue(TEXT("GlowColor"), DataColor);
	GlowMID->SetScalarParameterValue(TEXT("GlowAmount"), GlowStartAmount);

	for (FStageClearHiddenMesh& M : HiddenMeshes)
	{
		if (auto* Mesh = Cast<UMeshComponent>(M.Source.Get()))
		{
			Mesh->SetOverlayMaterial(GlowMID);
		}
	}
}

void AStageClearPresentationDirector::BeginDissolve(float Duration)
{
	using namespace StageClearPresentation;

	if (bDissolving)
		return;

	bDissolving = true;
	DissolveAge = 0.f;
	DissolveDuration = FMath::Max(MinDissolveDuration, Duration);

	// 分解する範囲（Playerと装備のBoundsの合計）。
	FBox Box(ForceInit);

	for (const FStageClearHiddenMesh& M : HiddenMeshes)
	{
		if (M.Source.IsValid())
		{
			Box += M.Source->Bounds.GetBox();
		}
	}

	if (!Box.IsValid)
	{
		Box = FBox(VictoryFocusPoint - DissolveFallbackMin, VictoryFocusPoint + DissolveFallbackMax);
	}

	DissolveMinZ = Box.Min.Z;
	DissolveHeight = FMath::Max(1.f, Box.Max.Z - Box.Min.Z);

	if (DataMaterial)
	{
		DataMID = UMaterialInstanceDynamic::Create(DataMaterial, this);
		DataMID->SetVectorParameterValue(TEXT("GlowColor"), DataColor);
		DataMID->SetScalarParameterValue(TEXT("CaptureMinZ"), DissolveMinZ);
		DataMID->SetScalarParameterValue(TEXT("CaptureHeight"), DissolveHeight);
		DataMID->SetScalarParameterValue(TEXT("DissolveProgress"), 0.f);
	}

	for (FStageClearHiddenMesh& M : HiddenMeshes)
	{
		UMeshComponent* Source = Cast<UMeshComponent>(M.Source.Get());
		if (!Source)
			continue;

		if (DataMID)
		{
			// 元Mesh・元Materialは変更せず、同じ姿勢の一時Proxyへデータ化Materialを掛けます。
			UMeshComponent* Proxy = nullptr;

			if (auto* Skel = Cast<USkeletalMeshComponent>(Source); Skel && Skel->GetSkeletalMeshAsset())
			{
				auto* P = NewObject<USkeletalMeshComponent>(Source->GetOwner(), NAME_None, RF_Transient);
				P->SetSkeletalMeshAsset(Skel->GetSkeletalMeshAsset());
				P->SetLeaderPoseComponent(Skel);
				Proxy = P;
			}
			else if (auto* Static = Cast<UStaticMeshComponent>(Source); Static && Static->GetStaticMesh())
			{
				auto* P = NewObject<UStaticMeshComponent>(Source->GetOwner(), NAME_None, RF_Transient);
				P->SetStaticMesh(Static->GetStaticMesh());
				Proxy = P;
			}

			if (Proxy)
			{
				Proxy->SetMobility(EComponentMobility::Movable);
				Proxy->SetupAttachment(Source->GetAttachParent(), Source->GetAttachSocketName());
				Proxy->SetRelativeTransform(Source->GetRelativeTransform());
				Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Proxy->SetGenerateOverlapEvents(false);
				Proxy->SetCanEverAffectNavigation(false);
				Proxy->SetCastShadow(false);

				for (int32 Slot = 0; Slot < FMath::Max(1, Source->GetNumMaterials()); ++Slot)
				{
					Proxy->SetMaterial(Slot, DataMID);
				}

				Proxy->RegisterComponent();

				if (!Source->GetAttachParent())
				{
					Proxy->SetWorldTransform(Source->GetComponentTransform());
				}

				M.DataProxy = Proxy;
			}
		}

		Source->SetOverlayMaterial(M.OriginalOverlay);
		Source->SetVisibility(false, false);
	}

	// データ粒子：Niagaraが生成・上昇・出現順（スキャン線の高さ順）を担当する。
	// C++は発生範囲（Player/網のBounds）・分解時間・色を渡し、実時間の経過をDesiredAgeで指定する。
	if (DataFragmentVFX && !IsValid(FragmentComponent))
	{
		FragmentComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, DataFragmentVFX, Box.GetCenter(), FRotator::ZeroRotator, FVector::OneVector, false, false);
		if (FragmentComponent)
		{
			FragmentComponent->SetVariableVec3(TEXT("User.SpawnSize"), Box.GetSize());
			FragmentComponent->SetVariableFloat(TEXT("User.DissolveDuration"), DissolveDuration);
			FragmentComponent->SetVariableLinearColor(TEXT("User.Color"), DataColor);
			FragmentComponent->SetVariableFloat(TEXT("User.Fade"), 1.f);
			FragmentComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
			FragmentComponent->SetCanRenderWhileSeeking(true);
			FragmentComponent->Activate(true);
		}
	}

	if (TransferVFX)
	{
		const FVector TransferLocation(VictoryFocusPoint.X, VictoryFocusPoint.Y, DissolveMinZ + TransferVFXLift);
		TransferComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, TransferVFX, TransferLocation, FRotator::ZeroRotator, FVector(TransferVFXScale),
			true, true, ENCPoolMethod::None, true);
		if (TransferComponent)
		{
			TransferComponent->SetVariableLinearColor(TEXT("User.Color"),
				FLinearColor(DataColor.R, DataColor.G, DataColor.B, TransferVFXAlpha));
		}
	}
}

void AStageClearPresentationDirector::FinishDissolve()
{
	bDissolveFinished = true;

	for (FStageClearHiddenMesh& M : HiddenMeshes)
	{
		if (IsValid(M.DataProxy))
		{
			M.DataProxy->DestroyComponent();
			M.DataProxy = nullptr;
		}
	}

	if (IsValid(TransferComponent))
	{
		TransferComponent->Deactivate();
	}

	UE_LOG(LogREBoarGetch, Log, TEXT("[StageClear] Playerと装備を消しました（%d個のMeshを非表示）"), HiddenMeshes.Num());
}

void AStageClearPresentationDirector::BeginCameraRetreat(float Duration, float InParticleFadeDuration)
{
	using namespace StageClearPresentation;

	if (bRetreating)
		return;

	bRetreating = true;
	RetreatAge = 0.f;
	RetreatDuration = FMath::Max(MinRetreatDuration, Duration);
	ParticleFadeDuration = FMath::Max(MinParticleFadeDuration, InParticleFadeDuration);
}

void AStageClearPresentationDirector::DestroyParticles()
{
	for (UNiagaraComponent* C : StarComponents)
	{
		if (IsValid(C))
		{
			C->DestroyComponent();
		}
	}

	StarComponents.Reset();

	if (IsValid(FragmentComponent))
	{
		FragmentComponent->DestroyComponent();
	}

	FragmentComponent = nullptr;

	if (IsValid(TransferComponent))
	{
		TransferComponent->DestroyComponent();
	}

	TransferComponent = nullptr;
}

void AStageClearPresentationDirector::BeginScreenFadeOut(float Duration)
{
	HideBanner(BannerFadeOutTime);

	if (bDissolving && !bDissolveFinished)
	{
		FinishDissolve();
	}

	DestroyParticles();

	if (APlayerController* PC = GetPresentationController())
	{
		if (PC->PlayerCameraManager)
		{
			const float FadeDuration = FMath::Max(StageClearPresentation::MinScreenFadeDuration, Duration);
			PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FadeDuration, FLinearColor::Black, false, true);
		}
	}
}

void AStageClearPresentationDirector::TickPresentation(float Dt)
{
	using namespace StageClearPresentation;

	MontageAge += Dt;

	if (bMontagePlaying || bFallbackJump)
	{
		UpdateMontage();
	}

	// ---- 発光（分解が始まるまで） ----
	if (bGlowing && GlowMID && !bDissolving)
	{
		GlowAge += Dt;
		GlowMID->SetScalarParameterValue(TEXT("GlowAmount"),
			FMath::Lerp(GlowStartAmount, 1.f, FMath::SmoothStep(0.f, GlowRamp, GlowAge)));
	}

	// ---- 分解 ----
	if (bDissolving)
	{
		DissolveAge += Dt;

		if (!bDissolveFinished)
		{
			const float P = FMath::Clamp(DissolveAge / DissolveDuration, 0.f, 1.f);

			if (DataMID)
			{
				DataMID->SetScalarParameterValue(TEXT("DissolveProgress"), FMath::Lerp(0.f, DissolveProgressEnd, P));
				DataMID->SetScalarParameterValue(TEXT("GlowAmount"), 1.f + DissolveGlowExtra * (1.f - P));
			}

			if (P >= 1.f)
			{
				FinishDissolve();
			}
		}

		// データ粒子は分解開始からの実時間で進める（個々の粒子の位置・表示はNiagara側）。
		if (IsValid(FragmentComponent))
		{
			FragmentComponent->SetDesiredAge(DissolveAge);
		}
	}

	// ---- カメラの後退と、最後に粒子を薄くする ----
	if (bRetreating)
	{
		RetreatAge += Dt;
		UpdateCamera(EvaluateRetreat(RetreatAge / RetreatDuration));

		const float FadeStart = RetreatDuration - ParticleFadeDuration;
		const float ParticleAlpha = 1.f - FMath::Clamp((RetreatAge - FadeStart) / ParticleFadeDuration, 0.f, 1.f);

		if (IsValid(FragmentComponent))
		{
			FragmentComponent->SetVariableFloat(TEXT("User.Fade"), ParticleAlpha);
		}

		if (RetreatAge >= FadeStart)
		{
			for (int32 I = 0; I < StarComponents.Num(); ++I)
			{
				if (!IsValid(StarComponents[I]))
					continue;

				StarComponents[I]->Deactivate();
				const FLinearColor C = StarColors.IsValidIndex(I) ? StarColors[I] : FLinearColor::White;
				StarComponents[I]->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(C.R, C.G, C.B, ParticleAlpha));
			}
		}
	}
}

void AStageClearPresentationDirector::RestoreVisuals()
{
	for (FStageClearHiddenMesh& M : HiddenMeshes)
	{
		if (IsValid(M.DataProxy))
		{
			M.DataProxy->DestroyComponent();
		}

		if (auto* Mesh = Cast<UMeshComponent>(M.Source.Get()))
		{
			Mesh->SetOverlayMaterial(M.OriginalOverlay);
			Mesh->SetHiddenInGame(M.bOriginalHiddenInGame);
			Mesh->SetVisibility(M.bOriginalVisible, false);
		}
	}

	HiddenMeshes.Reset();

	if (ABoarPlayerCharacter* Player = GetPresentationPlayer())
	{
		if (bFallbackJump)
		{
			Player->GetMesh()->SetRelativeLocation(MeshOriginalRelativeLocation);
		}

		if (bSingleNodePlayback)
		{
			Player->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}
		else if (UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance(); Anim && VictoryMontage && bMontagePlaying)
		{
			Anim->Montage_Stop(StageClearPresentation::StopMontageBlend, VictoryMontage);
		}
	}

	DestroyParticles();
	HideBanner(0.f);
}

// 勝利の瞬間に「STAGE CLEAR!」バナーを出します（HUDはBeginVictoryShotで非表示済み）。
void AStageClearPresentationDirector::ShowBanner()
{
	if (BannerWidget || !BannerWidgetClass)
		return;

	if (APlayerController* PC = GetPresentationController())
	{
		BannerWidget = CreateWidget<UBoarStageClearBannerWidget>(PC, BannerWidgetClass);
		if (BannerWidget)
		{
			BannerWidget->AddToViewport(BoarUILayer::Presentation);
		}
	}
}

void AStageClearPresentationDirector::HideBanner(float Duration)
{
	if (!BannerWidget)
		return;

	if (Duration > 0.f)
	{
		BannerWidget->HideBanner(Duration);
	}
	else
	{
		BannerWidget->RemoveFromParent();
	}

	BannerWidget = nullptr;
}

void AStageClearPresentationDirector::RestoreCamera()
{
	if (APlayerController* PC = GetPresentationController())
	{
		if (VictoryCamera && PC->GetViewTarget() == VictoryCamera)
		{
			PC->SetViewTarget(OriginalViewTarget.IsValid() ? OriginalViewTarget.Get() : PC->GetPawn());
		}

		// カットで戻した後に外部ViewTargetの登録を外す（ACFの通常カメラは裏で更新済みなので最新の構図から再開する）。
		if (VictoryCamera)
		{
			if (UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(PC))
			{
				Cameras->RemoveExternalViewTarget(VictoryCamera, this);
			}
		}
	}

	if (IsValid(VictoryCamera))
	{
		VictoryCamera->Destroy();
	}

	VictoryCamera = nullptr;
}
