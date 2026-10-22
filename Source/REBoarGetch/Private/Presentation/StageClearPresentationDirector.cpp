#include "Presentation/StageClearPresentationDirector.h"
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

AStageClearPresentationDirector::AStageClearPresentationDirector()
{
	StarColors = { FLinearColor(1.f, .82f, .08f), FLinearColor(1.f, 1.f, 1.f), FLinearColor(.05f, .85f, 1.f) };
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
	if (!Player) return;
	TArray<AActor*> Actors = { Player };
	// 網などの装備はMesh Socketへ子Actorとしてアタッチされています。孫Actorまで辿り、網だけ残らないようにします。
	for (int32 I = 0; I < Actors.Num(); ++I)
	{
		TArray<AActor*> Attached;
		Actors[I]->GetAttachedActors(Attached, false);
		for (AActor* A : Attached) Actors.AddUnique(A);
	}
	for (AActor* A : Actors)
	{
		TArray<UMeshComponent*> Meshes;
		A->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame || (!Cast<USkeletalMeshComponent>(Mesh) && !Cast<UStaticMeshComponent>(Mesh))) continue;
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
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	APlayerController* PC = GetPresentationController();
	if (bVictoryShot || !Player || !PC) return;
	bVictoryShot = true;
	SetPlayerHUDHidden(true);
	CollectMeshes();
	MeshOriginalRelativeLocation = Player->GetMesh()->GetRelativeLocation();

	// 消失地点は開始時に固定し、以後プレイヤーやボーンの動きに追従させません。
	const float HalfHeight = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	VictoryFocusPoint = Player->GetActorLocation() - FVector(0, 0, HalfHeight) + VictoryFocusOffset;

	// 遮蔽物の少ない向きを選び、カメラが壁へめり込まないようにします。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(StageClearCamera), false, Player);
	TArray<AActor*> Ignored; Player->GetAttachedActors(Ignored, true);
	Params.AddIgnoredActors(Ignored);
	const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
	const float Candidates[] = { CameraYawOffset, -CameraYawOffset, CameraYawOffset + 45.f, -CameraYawOffset - 45.f, 0.f };
	float BestFraction = -1.f, BestScore = -1.f;
	const FVector CurrentCamera = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : VictoryFocusPoint;
	for (float Yaw : Candidates)
	{
		const FVector Dir = Forward.RotateAngleAxis(Yaw, FVector::UpVector);
		const FVector Target = VictoryFocusPoint + Dir * (CameraDistance + RetreatDistance) + FVector(0, 0, CameraHeight + RetreatHeight);
		FHitResult Hit;
		const bool bHit = GetWorld()->SweepSingleByChannel(Hit, VictoryFocusPoint, Target, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(15.f), Params);
		const float Fraction = bHit ? Hit.Time : 1.f;
		// Blendは直線補間なので、現在のカメラから開始位置までが通るかも評価します（檻などを突き抜けないように）。
		const FVector Start = VictoryFocusPoint + Dir * CameraDistance + FVector(0, 0, CameraHeight);
		const bool bBlendBlocked = GetWorld()->SweepTestByChannel(CurrentCamera, Start, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(10.f), Params);
		const float Score = Fraction + (bBlendBlocked ? 0.f : .5f);
		if (Score > BestScore + .05f) { BestScore = Score; BestFraction = Fraction; CameraDirection = Dir; }
		if (!bHit && !bBlendBlocked) break;
	}
	// 最終位置まで通らない場合は、通れる距離だけ後退します（近距離側は維持）。
	const float Full = FVector(CameraDistance + RetreatDistance, 0, CameraHeight + RetreatHeight).Size();
	const float Near = FVector(CameraDistance, 0, CameraHeight).Size();
	const float Allowed = FMath::Max(Near, Full * BestFraction - 20.f);
	const float Scale = Full > Near ? FMath::Clamp((Allowed - Near) / (Full - Near), 0.f, 1.f) : 1.f;
	MaxRetreatDistance = RetreatDistance * Scale;
	MaxRetreatHeight = RetreatHeight * Scale;

	FActorSpawnParameters SpawnParams; SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	VictoryCamera = GetWorld()->SpawnActor<ACameraActor>(ComputeCameraLocation(0.f), (VictoryFocusPoint - ComputeCameraLocation(0.f)).Rotation(), SpawnParams);
	if (VictoryCamera)
	{
		VictoryCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
		VictoryCamera->GetCameraComponent()->SetFieldOfView(VictoryFOV);
		OriginalViewTarget = PC->GetViewTarget();
		PC->SetViewTargetWithBlend(VictoryCamera, CameraBlendTime, VTBlend_Cubic);
	}
	UE_LOG(LogTemp, Log, TEXT("[StageClear] Victory start Focus=%s RetreatScale=%.2f"), *VictoryFocusPoint.ToString(), Scale);
}

FVector AStageClearPresentationDirector::ComputeCameraLocation(float A) const
{
	return VictoryFocusPoint + CameraDirection * (CameraDistance + MaxRetreatDistance * A) + FVector(0, 0, CameraHeight + MaxRetreatHeight * A);
}

float AStageClearPresentationDirector::EvaluateRetreat(float T) const
{
	T = FMath::Clamp(T, 0.f, 1.f);
	if (RetreatCurve) return FMath::Clamp(RetreatCurve->GetFloatValue(T), 0.f, 1.f);
	return FMath::InterpEaseInOut(0.f, 1.f, T, RetreatEaseExponent);
}

void AStageClearPresentationDirector::UpdateCamera(float A)
{
	if (!VictoryCamera) return;
	const FVector Location = ComputeCameraLocation(A);
	VictoryCamera->SetActorLocationAndRotation(Location, (VictoryFocusPoint - Location).Rotation());
	VictoryCamera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(VictoryFOV, RetreatEndFOV, A));
}

void AStageClearPresentationDirector::PlayVictoryMontage()
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player || bMontagePlaying || bFallbackJump) return;
	MontageAge = 0.f;
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	const float Length = (Anim && VictoryMontage) ? Anim->Montage_Play(VictoryMontage, MontagePlayRate) : 0.f;
	bMontagePlaying = Length > 0.f;
	bFallbackJump = !bMontagePlaying;
	if (bFallbackJump) UE_LOG(LogTemp, Warning, TEXT("[StageClear] Victory montage unavailable; using fallback hop."));
}

void AStageClearPresentationDirector::UpdateMontage()
{
	ABoarPlayerCharacter* Player = GetPresentationPlayer();
	if (!Player) return;
	if (bMontagePlaying && !bSingleNodePlayback && bAllowSingleNodeFallback && ++MontageCheckFrames == 2)
	{
		// Slotが現在のAnimGraph経路に無いとMontageは見た目に反映されません。その場合は同じアニメをSingle Nodeで再生します。
		UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
		const FName Slot = VictoryMontage->SlotAnimTracks.IsEmpty() ? NAME_None : VictoryMontage->SlotAnimTracks[0].SlotName;
		UAnimSequenceBase* Sequence = VictoryAnimation.Get();
		if (!Sequence && !VictoryMontage->SlotAnimTracks.IsEmpty() && !VictoryMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
			Sequence = VictoryMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
		if (Anim && Sequence && Anim->GetSlotNodeGlobalWeight(Slot) <= KINDA_SMALL_NUMBER)
		{
			const float Position = Anim->Montage_GetPosition(VictoryMontage);
			Anim->Montage_Stop(0.f, VictoryMontage);
			Player->GetMesh()->PlayAnimation(Sequence, false);
			Player->GetMesh()->SetPosition(Position, false);
			Player->GetMesh()->SetPlayRate(MontagePlayRate);
			bSingleNodePlayback = true;
			bMontageHeld = true;  // 非ループのSingle Nodeは最終フレームで停止し、掲げた姿勢を保持します。
			UE_LOG(LogTemp, Log, TEXT("[StageClear] Slot '%s' is not active in the AnimGraph; playing %s as single node."), *Slot.ToString(), *GetNameSafe(Sequence));
		}
	}
	if (bMontagePlaying && !bMontageHeld)
	{
		// 最終ポーズ（網を掲げた姿勢）で止め、消失が終わるまで通常AnimBPへ戻しません。
		UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
		const float HoldAt = VictoryMontage->GetPlayLength() - FMath::Max(.05f, VictoryMontage->BlendOut.GetBlendTime() + .05f);
		if (Anim && Anim->Montage_IsPlaying(VictoryMontage) && Anim->Montage_GetPosition(VictoryMontage) >= HoldAt) { Anim->Montage_Pause(VictoryMontage); bMontageHeld = true; }
	}
	if (bFallbackJump)
	{
		const float H = FallbackJumpHeight * FMath::Sin(PI * FMath::Clamp((MontageAge - FallbackJumpDelay) / .55f, 0.f, 1.f));
		Player->GetMesh()->SetRelativeLocation(MeshOriginalRelativeLocation + FVector(0, 0, H));
	}
}

void AStageClearPresentationDirector::SpawnStarBurst()
{
	if (!StarVFX) return;
	// 網Meshの上端（掲げた先端）から放ちます。網が無い場合は頭上を使います。
	FVector Origin = VictoryFocusPoint + FVector(0, 0, 110);
	if (ABoarPlayerCharacter* Player = GetPresentationPlayer())
	{
		float BestZ = -BIG_NUMBER;
		for (const FStageClearHiddenMesh& M : HiddenMeshes)
			if (M.Source.IsValid() && M.Source->GetOwner() != Player)
			{
				const FBoxSphereBounds B = M.Source->Bounds;
				if (B.Origin.Z + B.BoxExtent.Z > BestZ) { BestZ = B.Origin.Z + B.BoxExtent.Z; Origin = FVector(B.Origin.X, B.Origin.Y, BestZ); }
			}
	}
	Origin += StarOffset;
	for (int32 I = 0; I < StarColors.Num(); ++I)
	{
		UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, StarVFX, Origin, FRotator(0, 120.f * I, 0), FVector(StarScale), true, true, ENCPoolMethod::None, true);
		if (!C) continue;
		C->SetVariableLinearColor(TEXT("User.Color"), StarColors[I]);
		C->SetVariableFloat(TEXT("User.Seed"), float(I));
		StarComponents.Add(C);
	}
}

void AStageClearPresentationDirector::BeginGlow(float RampDuration)
{
	if (bGlowing || !GlowMaterial) return;
	bGlowing = true;
	GlowAge = 0.f;
	GlowRamp = FMath::Max(.01f, RampDuration);
	GlowMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
	GlowMID->SetVectorParameterValue(TEXT("GlowColor"), DataColor);
	GlowMID->SetScalarParameterValue(TEXT("GlowAmount"), .1f);
	for (FStageClearHiddenMesh& M : HiddenMeshes)
		if (auto* Mesh = Cast<UMeshComponent>(M.Source.Get())) Mesh->SetOverlayMaterial(GlowMID);
}

void AStageClearPresentationDirector::BeginDissolve(float Duration)
{
	if (bDissolving) return;
	bDissolving = true;
	DissolveAge = 0.f;
	DissolveDuration = FMath::Max(.05f, Duration);
	FBox Box(ForceInit);
	for (const FStageClearHiddenMesh& M : HiddenMeshes) if (M.Source.IsValid()) Box += M.Source->Bounds.GetBox();
	if (!Box.IsValid) Box = FBox(VictoryFocusPoint - FVector(40, 40, 70), VictoryFocusPoint + FVector(40, 40, 110));
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
		if (!Source) continue;
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
				for (int32 Slot = 0; Slot < FMath::Max(1, Source->GetNumMaterials()); ++Slot) Proxy->SetMaterial(Slot, DataMID);
				Proxy->RegisterComponent();
				if (!Source->GetAttachParent()) Proxy->SetWorldTransform(Source->GetComponentTransform());
				M.DataProxy = Proxy;
			}
		}
		Source->SetOverlayMaterial(M.OriginalOverlay);
		Source->SetVisibility(false, false);
	}
	// キューブ状のデータ粒子：スキャン線が通過した高さから順に舞い上がります。
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube && GlowMaterial)
	{
		FragmentMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		FragmentMID->SetVectorParameterValue(TEXT("GlowColor"), DataColor);
		FragmentMID->SetScalarParameterValue(TEXT("GlowStrength"), 9.f);
		FRandomStream Random(int32(VictoryFocusPoint.X * 13.f + VictoryFocusPoint.Y * 7.f));
		for (int32 I = 0; I < FMath::Clamp(FragmentCount, 0, 64); ++I)
		{
			auto* Piece = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Piece->SetMobility(EComponentMobility::Movable);
			Piece->SetStaticMesh(Cube);
			Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Piece->SetGenerateOverlapEvents(false);
			Piece->SetCanEverAffectNavigation(false);
			Piece->SetCastShadow(false);
			Piece->SetMaterial(0, FragmentMID);
			Piece->SetUsingAbsoluteLocation(true); Piece->SetUsingAbsoluteRotation(true); Piece->SetUsingAbsoluteScale(true);
			Piece->RegisterComponent();
			const float S = Random.FRandRange(.035f, .09f);
			Piece->SetWorldScale3D(FVector(S));
			Piece->SetWorldRotation(FRotator(Random.FRandRange(0, 90), Random.FRandRange(0, 90), 0));
			FStageClearFragment F; F.Mesh = Piece;
			F.Start = FVector(Random.FRandRange(Box.Min.X, Box.Max.X), Random.FRandRange(Box.Min.Y, Box.Max.Y), Random.FRandRange(Box.Min.Z, Box.Max.Z));
			F.Drift = FVector(Random.FRandRange(-30.f, 30.f), Random.FRandRange(-30.f, 30.f), FragmentRiseSpeed * Random.FRandRange(.6f, 1.4f));
			F.Delay = DissolveDuration * FMath::Clamp((F.Start.Z - DissolveMinZ) / DissolveHeight, 0.f, 1.f) * .9f;
			Piece->SetWorldLocation(F.Start);
			Piece->SetVisibility(false);
			Fragments.Add(F);
		}
	}
	if (TransferVFX)
	{
		TransferComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TransferVFX, FVector(VictoryFocusPoint.X, VictoryFocusPoint.Y, DissolveMinZ + 2.f), FRotator::ZeroRotator, FVector(.7f), true, true, ENCPoolMethod::None, true);
		if (TransferComponent) TransferComponent->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(DataColor.R, DataColor.G, DataColor.B, .85f));
	}
}

void AStageClearPresentationDirector::FinishDissolve()
{
	bDissolveFinished = true;
	for (FStageClearHiddenMesh& M : HiddenMeshes)
		if (IsValid(M.DataProxy)) { M.DataProxy->DestroyComponent(); M.DataProxy = nullptr; }
	if (IsValid(TransferComponent)) TransferComponent->Deactivate();
	UE_LOG(LogTemp, Log, TEXT("[StageClear] Player and equipment vanished (%d meshes hidden)"), HiddenMeshes.Num());
}

void AStageClearPresentationDirector::BeginCameraRetreat(float Duration, float InParticleFadeDuration)
{
	if (bRetreating) return;
	bRetreating = true;
	RetreatAge = 0.f;
	RetreatDuration = FMath::Max(.1f, Duration);
	ParticleFadeDuration = FMath::Max(.05f, InParticleFadeDuration);
}

void AStageClearPresentationDirector::DestroyParticles()
{
	for (UNiagaraComponent* C : StarComponents) if (IsValid(C)) C->DestroyComponent();
	StarComponents.Reset();
	for (FStageClearFragment& F : Fragments) if (IsValid(F.Mesh)) F.Mesh->DestroyComponent();
	Fragments.Reset();
	if (IsValid(TransferComponent)) TransferComponent->DestroyComponent();
	TransferComponent = nullptr;
}

void AStageClearPresentationDirector::BeginScreenFadeOut(float Duration)
{
	if (bDissolving && !bDissolveFinished) FinishDissolve();
	DestroyParticles();
	if (APlayerController* PC = GetPresentationController())
		if (PC->PlayerCameraManager) PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FMath::Max(0.01f, Duration), FLinearColor::Black, false, true);
}

void AStageClearPresentationDirector::TickPresentation(float Dt)
{
	MontageAge += Dt;
	if (bMontagePlaying || bFallbackJump) UpdateMontage();
	if (bGlowing && GlowMID && !bDissolving)
	{
		GlowAge += Dt;
		GlowMID->SetScalarParameterValue(TEXT("GlowAmount"), FMath::Lerp(.1f, 1.f, FMath::SmoothStep(0.f, GlowRamp, GlowAge)));
	}
	if (bDissolving)
	{
		DissolveAge += Dt;
		if (!bDissolveFinished)
		{
			const float P = FMath::Clamp(DissolveAge / DissolveDuration, 0.f, 1.f);
			if (DataMID)
			{
				DataMID->SetScalarParameterValue(TEXT("DissolveProgress"), FMath::Lerp(0.f, 1.02f, P));
				DataMID->SetScalarParameterValue(TEXT("GlowAmount"), 1.f + .5f * (1.f - P));
			}
			if (P >= 1.f) FinishDissolve();
		}
		// スキャン線が通過した順にデータ粒子が出現し、上昇します。
		for (FStageClearFragment& F : Fragments)
			if (IsValid(F.Mesh))
			{
				const float A = DissolveAge - F.Delay;
				F.Mesh->SetVisibility(A >= 0.f);
				if (A >= 0.f) F.Mesh->SetWorldLocation(F.Start + F.Drift * A);
			}
	}
	if (bRetreating)
	{
		RetreatAge += Dt;
		UpdateCamera(EvaluateRetreat(RetreatAge / RetreatDuration));
		const float FadeStart = RetreatDuration - ParticleFadeDuration;
		const float ParticleAlpha = 1.f - FMath::Clamp((RetreatAge - FadeStart) / ParticleFadeDuration, 0.f, 1.f);
		if (FragmentMID) FragmentMID->SetScalarParameterValue(TEXT("GlowAmount"), .9f * ParticleAlpha);
		if (RetreatAge >= FadeStart)
			for (int32 I = 0; I < StarComponents.Num(); ++I)
				if (IsValid(StarComponents[I]))
				{
					StarComponents[I]->Deactivate();
					const FLinearColor C = StarColors.IsValidIndex(I) ? StarColors[I] : FLinearColor::White;
					StarComponents[I]->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(C.R, C.G, C.B, ParticleAlpha));
				}
	}
}

void AStageClearPresentationDirector::RestoreVisuals()
{
	for (FStageClearHiddenMesh& M : HiddenMeshes)
	{
		if (IsValid(M.DataProxy)) M.DataProxy->DestroyComponent();
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
		if (bFallbackJump) Player->GetMesh()->SetRelativeLocation(MeshOriginalRelativeLocation);
		if (bSingleNodePlayback) Player->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		else if (UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance(); Anim && VictoryMontage && bMontagePlaying) Anim->Montage_Stop(.1f, VictoryMontage);
	}
	DestroyParticles();
}

void AStageClearPresentationDirector::RestoreCamera()
{
	if (APlayerController* PC = GetPresentationController())
		if (VictoryCamera && PC->GetViewTarget() == VictoryCamera)
			PC->SetViewTarget(OriginalViewTarget.IsValid() ? OriginalViewTarget.Get() : PC->GetPawn());
	if (IsValid(VictoryCamera)) VictoryCamera->Destroy();
	VictoryCamera = nullptr;
}
