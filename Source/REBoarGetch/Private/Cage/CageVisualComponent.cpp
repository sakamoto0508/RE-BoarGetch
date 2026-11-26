#include "Cage/CageVisualComponent.h"

#include "Cage/Cage.h"
#include "UI/CageHealthWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Presentation/BoarImpactShockwave.h"
#include "UObject/ConstructorHelpers.h"
#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraSubsystem.h"
#include "Effects/ACFCameraEffect.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogCageVisual, Log, All);

namespace CageVisual
{
	/** 見た目の更新間隔（秒）。HP表示なので毎フレームである必要はない。 */
	constexpr float TickInterval = .05f;

	/** 残像がHPに追いついたとみなす差。 */
	constexpr float TrailEpsilon = .001f;
}

UCageVisualComponent::UCageVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = CageVisual::TickInterval;

	// 見た目専用のMesh。Collision・Navigation・Overlapには一切関与させない。
	CageMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CageMesh"));
	CageMeshComponent->SetupAttachment(this);
	CageMeshComponent->bUseDefaultCollision = false;
	CageMeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
	CageMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CageMeshComponent->SetCanEverAffectNavigation(false);
	CageMeshComponent->SetGenerateOverlapEvents(false);

	// 頭上のHPゲージ。必要なときだけ手動で再描画する。
	Gauge = CreateDefaultSubobject<UWidgetComponent>(TEXT("CageHPGauge"));
	Gauge->SetupAttachment(this);
	Gauge->SetWidgetSpace(EWidgetSpace::World);
	Gauge->SetDrawSize(GaugeDrawSize);
	Gauge->SetPivot(FVector2D(.5, .5));
	Gauge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gauge->SetCanEverAffectNavigation(false);
	Gauge->SetGenerateOverlapEvents(false);
	Gauge->SetTwoSided(true);
	Gauge->SetManuallyRedraw(true);
	Gauge->SetTickWhenOffscreen(false);

	// 既定のVFX。BP_CageのCagePresentation（Details）で差し替え・解除できる。命中はプレイヤー被弾と同じ衝撃波を共有する。
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> HitFinder(TEXT("/Game/REBoarGetch/Art/VFX/Player/NS_PlayerHitImpact"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> BreakFinder(TEXT("/Game/REBoarGetch/Art/VFX/Cage/NS_CageBreak"));
	HitVFX = HitFinder.Object;
	BreakVFX = BreakFinder.Object;
	static ConstructorHelpers::FObjectFinder<UACFCameraEffectPreset> CameraFinder(TEXT("/Game/DataAssets/Camera/DA_PlayerHitCameraEffect"));
	BreakCameraEffect = CameraFinder.Object;
}

void UCageVisualComponent::OnRegister()
{
	Super::OnRegister();

	CageMeshComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);

	if (!CageMeshComponent->IsRegistered())
	{
		CageMeshComponent->RegisterComponent();
	}

	ApplyVisualSource();

	Gauge->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);

	// 代替Widgetは使わない。未設定ならWidgetを生成せず、BeginPlayでErrorを出す。
	Gauge->SetWidgetClass(GaugeWidgetClass);
	Gauge->SetDrawSize(GaugeDrawSize);
	Gauge->SetRelativeLocation(FVector(0, 0, GaugeHeight));
	Gauge->SetRelativeScale3D(FVector(GaugeWorldScale));

	if (!Gauge->IsRegistered())
	{
		Gauge->RegisterComponent();
	}
}

void UCageVisualComponent::BeginPlay()
{
	Super::BeginPlay();

	Cage = Cast<ACage>(GetOwner());
	if (!Cage)
	{
		SetComponentTickEnabled(false);
		return;
	}

	RestRelativeLocation = GetRelativeLocation();

#if WITH_EDITOR
	// エディタでは保存済みのNiagaraのコンパイル結果が古いと、初回の再生時にコンパイル待ちになり、
	// 檻が壊れた瞬間の演出が約1秒遅れる（見逃す）。プレイ開始時に済ませておく（Cook済みのゲームでは不要）。
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		for (UNiagaraSystem* System : {HitVFX.Get(), BreakVFX.Get()})
		{
			if (System)
				System->WaitForCompilationComplete(false, false);
		}
	}
#endif

	// 代替の見た目は作らない。未設定なら見た目（ランプ含む）だけを無効化し、HP・破壊処理は継続する。
	if (!CageMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("[CageVisual] %s.%s: CageMesh（SM_Cage）が未設定です。檻の見た目とランプを省きます（HP・破壊処理は継続します）。"),
			*GetNameSafe(GetOwner()), *GetName());
	}
	else
	{
		for (int32 Slot : LampMaterialSlots)
		{
			if (Slot >= 0 && Slot < CageMeshComponent->GetNumMaterials())
			{
				Lamps.Add(CageMeshComponent->CreateDynamicMaterialInstance(Slot));
			}
		}
	}

	InitGauge();

	Fraction = FMath::Clamp(Cage->GetHP() / FMath::Max(Cage->GetMaxHP(), 1.f), 0.f, 1.f);
	Trail = Fraction;

	Cage->OnHealthChanged.AddUniqueDynamic(this, &UCageVisualComponent::HealthChanged);
	Cage->OnCageDestroyed.AddUniqueDynamic(this, &UCageVisualComponent::Destroyed);
	Cage->OnRespawned.AddUniqueDynamic(this, &UCageVisualComponent::Respawned);
	bDestroyed = Cage->GetIsCageDestroyed();
}

void UCageVisualComponent::InitGauge()
{
	bGaugeAvailable = false;

	// 欠けているものを1つだけ特定してErrorに出す。
	FString Missing;

	// 基底C++クラスはレイアウトを持たないため、WBP（非Native）の指定を必須にする。
	if (!GaugeWidgetClass || GaugeWidgetClass->HasAnyClassFlags(CLASS_Native))
	{
		Missing = TEXT("GaugeWidgetClass（WBP_CageHealth）");
	}
	else
	{
		Gauge->InitWidget();
		const auto* Widget = Cast<UCageHealthWidget>(Gauge->GetUserWidgetObject());
		if (!Widget)
		{
			Missing = FString::Printf(TEXT("%s のWidgetインスタンス"), *GaugeWidgetClass->GetName());
		}
		else if (!Widget->HasRequiredBindings())
		{
			Missing = FString::Printf(TEXT("%s.HealthBar（ProgressBarのBinding）"), *GaugeWidgetClass->GetName());
		}
	}

	if (!Missing.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[CageVisual] %s.%s: %s がありません。HPゲージを省きます（HP・破壊処理は継続します）。"),
			*GetNameSafe(GetOwner()), *GetName(), *Missing);
		Gauge->SetWidget(nullptr);
		Gauge->SetVisibility(false);
		return;
	}

	bGaugeAvailable = true;
}

// 専用Meshを表示します。未設定なら何も表示しません（BeginPlayでErrorを出します）。
void UCageVisualComponent::ApplyVisualSource()
{
	CageMeshComponent->SetStaticMesh(CageMesh);
	CageMeshComponent->SetRelativeTransform(CageMeshTransform);
	CageMeshComponent->SetVisibility(CageMesh != nullptr);
}

void UCageVisualComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Cage)
	{
		Cage->OnHealthChanged.RemoveDynamic(this, &UCageVisualComponent::HealthChanged);
		Cage->OnCageDestroyed.RemoveDynamic(this, &UCageVisualComponent::Destroyed);
		Cage->OnRespawned.RemoveDynamic(this, &UCageVisualComponent::Respawned);
	}

	Super::EndPlay(Reason);
}

void UCageVisualComponent::HealthChanged(float Hp, float MaxHp)
{
	const float Next = FMath::Clamp(Hp / FMath::Max(MaxHp, 1.f), 0.f, 1.f);

	// 減ったときだけ被弾フラッシュを出す。
	if (Next < Fraction)
	{
		FlashRemaining = DamageFlashDuration;
	}

	Fraction = Next;

	// 回復したときは残像も即座に追いつかせる。
	if (Fraction > Trail)
	{
		Trail = Fraction;
	}

	bRedraw = true;
}

void UCageVisualComponent::Destroyed()
{
	bDestroyed = true;
	FlashRemaining = 0;
	Gauge->SetVisibility(false);
}

void UCageVisualComponent::Respawned()
{
	bDestroyed = false;
	Trail = Fraction;
	FlashRemaining = 0;
	bRedraw = true;
}

void UCageVisualComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt, Type, TickFunction);

	UpdateHitShake(Dt);

	if (!Cage || bDestroyed)
		return;

	auto* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera)
		return;

	// ゲージは一定距離内でだけ表示し、常にカメラの方を向ける。
	if (bGaugeAvailable)
	{
		const FVector Delta = Camera->GetCameraLocation() - Gauge->GetComponentLocation();
		const bool bGaugeVisible = DisplayDistance <= 0 || Delta.SizeSquared() <= FMath::Square(DisplayDistance);
		Gauge->SetVisibility(bGaugeVisible);

		if (bGaugeVisible)
		{
			Gauge->SetWorldRotation(Delta.Rotation());
		}
	}

	const bool bAnimating = FlashRemaining > 0 || Trail > Fraction + CageVisual::TrailEpsilon || Fraction <= LowThreshold;
	FlashRemaining = FMath::Max(0.f, FlashRemaining - Dt);
	Trail = FMath::FInterpConstantTo(Trail, Fraction, Dt, TrailCatchUpSpeed);

	if (!bRedraw && !bAnimating)
		return;

	// 低HP中は0〜1で点滅させる。
	const bool bLow = Fraction > 0 && Fraction <= LowThreshold;
	const float Pulse = bLow ? .5f + .5f * FMath::Sin(GetWorld()->GetTimeSeconds() * LowHealthPulseSpeed) : 0.f;

	FLinearColor Color = GaugeHighColor;

	if (bLow)
	{
		Color = GaugeLowColor;
	}
	else if (Fraction <= MediumThreshold)
	{
		Color = GaugeMediumColor;
	}

	const float Flash = FlashRemaining / DamageFlashDuration;

	if (bGaugeAvailable)
	{
		if (auto* W = Cast<UCageHealthWidget>(Gauge->GetUserWidgetObject()))
		{
			W->SetPresentation(Fraction, Trail, Color, Flash, Pulse);
		}

		Gauge->RequestRedraw();
	}

	for (UMaterialInstanceDynamic* M : Lamps)
	{
		if (!M)
			continue;

		const FLinearColor BaseTint = bLow ? LampLowTint : LampNormalTint;
		M->SetVectorParameterValue(TEXT("Tint"), FLinearColor::LerpUsingHSV(BaseTint, FLinearColor::White, Flash));
		M->SetScalarParameterValue(TEXT("Intensity"), bLow ? LampLowIntensity + Pulse * LampLowPulseIntensity : LampNormalIntensity);
	}

	bRedraw = false;
}

//-------------------------------------------------
// VFX（演出のみ）
//-------------------------------------------------

FVector UCageVisualComponent::GetCageCenter() const
{
	if (CageMesh && CageMeshComponent && CageMeshComponent->IsRegistered())
		return CageMeshComponent->Bounds.Origin;

	return GetComponentLocation() + FVector(0, 0, 100);
}

void UCageVisualComponent::PlayHitVFX(const FVector& Location, const FVector& Direction)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
		return;
	if (!HitVFX)
	{
		UE_LOG(LogCageVisual, Warning, TEXT("[CageVFX] %s: HitVFXが未設定のため攻撃命中VFXを出しません。"), *GetNameSafe(GetOwner()));
		return;
	}

	// 複数のBoarが同時に当たっても、同じ瞬間に何重にも重ねない。
	const double Now = World->GetTimeSeconds();
	if (Now - LastHitVFXTime < MinHitVFXInterval)
		return;

	// 押し込む向きが分からない（BP・テストからのApplyDamage）ときは、カメラから檻へ向かう向きにする。
	APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	FVector PushDirection = Direction.GetSafeNormal2D();
	if (PushDirection.IsNearlyZero() && Camera)
		PushDirection = (Location - Camera->GetCameraLocation()).GetSafeNormal2D();

	// 揺れは連続ヒットの間引きと関係なく、攻撃が通るたびに出す。
	StartHitShake(PushDirection);

	FBoarImpactShockwaveParams Params;
	Params.Scale = HitVFXScale;
	Params.Color = HitVFXColor;
	Params.RingCameraBias = HitVFXRingCameraBias;
	Params.bOverrideSparkColor = true;
	Params.SparkColor = HitVFXSparkColor;
	Params.StreakCount = HitVFXStreakCount;
	Params.ParticleCount = HitVFXParticleCount;
	Params.MaxRadius = HitVFXMaxRadius;
	Params.Lifetime = HitVFXLifetime;

	// 柵の内側に埋もれないよう、命中位置を攻撃側（押し込む向きの逆）へ寄せる。
	// 突進したBoarの体の陰にならないよう背中より上（床からHitVFXMinHeight以上）に上げ、さらにカメラ側へ少し寄せる。
	FVector SpawnLocation = Location - PushDirection * HitVFXSurfaceOffset;
	const float FloorZ = GetOwner() ? GetOwner()->GetActorLocation().Z : GetComponentLocation().Z;
	SpawnLocation.Z = FMath::Max(SpawnLocation.Z, FloorZ + HitVFXMinHeight);
	if (Camera)
		SpawnLocation += (Camera->GetCameraLocation() - SpawnLocation).GetSafeNormal() * HitVFXCameraPull;

	const bool bSpawned = BoarImpactShockwave::Spawn(World, HitVFX, SpawnLocation, PushDirection, Camera, Params) != nullptr;
	UE_LOG(LogCageVisual, Log, TEXT("[CageVFX] %s 攻撃命中VFX %s at %s"), *GetNameSafe(GetOwner()),
		bSpawned ? TEXT("再生") : TEXT("生成失敗"), *SpawnLocation.ToCompactString());
	if (bSpawned)
	{
		LastHitVFXTime = Now;
		++HitVFXSpawnCount;
	}
}

void UCageVisualComponent::StartHitShake(const FVector& WorldDirection)
{
	if (HitShakeAmplitude <= 0.f)
		return;

	// 揺れ始めるときだけ静止位置を覚える（揺れの途中で次が当たっても、ずれた位置を基準にしない）。
	if (ShakeRemaining <= 0.f)
		RestRelativeLocation = GetRelativeLocation();

	const FVector Dir = WorldDirection.IsNearlyZero() ? FVector::ForwardVector : WorldDirection.GetSafeNormal();
	const USceneComponent* Parent = GetAttachParent();
	ShakeLocalDirection = Parent ? Parent->GetComponentTransform().InverseTransformVectorNoScale(Dir) : Dir;
	ShakeRemaining = HitShakeDuration;

	// 普段は0.05秒間隔のTickなので、揺れている間だけ毎フレームTickにする。
	SetComponentTickInterval(0.f);
	SetComponentTickEnabled(true);
}

void UCageVisualComponent::UpdateHitShake(float Dt)
{
	if (ShakeRemaining <= 0.f)
		return;

	ShakeRemaining = FMath::Max(0.f, ShakeRemaining - Dt);
	if (ShakeRemaining <= 0.f || bDestroyed)
	{
		ShakeRemaining = 0.f;
		SetRelativeLocation(RestRelativeLocation);
		SetComponentTickInterval(CageVisual::TickInterval);
		return;
	}

	// 押し込まれる向きへ一度大きく凹み、減衰しながら数回揺れて戻る。
	const float T = 1.f - ShakeRemaining / HitShakeDuration;
	const float Offset = HitShakeAmplitude * FMath::Square(1.f - T) * FMath::Sin(T * HitShakeOscillations * UE_TWO_PI);
	SetRelativeLocation(RestRelativeLocation + ShakeLocalDirection * Offset);
}

void UCageVisualComponent::PlayBreakVFX()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
		return;
	if (!BreakVFX)
	{
		UE_LOG(LogCageVisual, Warning, TEXT("[CageVFX] %s: BreakVFXが未設定のため破壊VFXを出しません。"), *GetNameSafe(GetOwner()));
		return;
	}

	// 足元（Actorの原点＝檻の床）から出す。大きさは見た目のMeshから決め、未設定ならSM_Cage相当の既定値。
	const FVector Root = GetOwner() ? GetOwner()->GetActorLocation() : GetComponentLocation();
	float Footprint = 190.f;
	float CenterHeight = 150.f;
	if (CageMesh && CageMeshComponent && CageMeshComponent->IsRegistered())
	{
		const FBoxSphereBounds& B = CageMeshComponent->Bounds;
		Footprint = FMath::Clamp(FMath::Max(B.BoxExtent.X, B.BoxExtent.Y) * .9f, 80.f, 450.f);
		CenterHeight = FMath::Clamp(static_cast<float>(B.Origin.Z - Root.Z), 60.f, 400.f);
	}

	if (BreakFootprintRadiusOverride > 0)
		Footprint = BreakFootprintRadiusOverride;
	if (BreakCenterHeightOverride > 0)
		CenterHeight = BreakCenterHeightOverride;

	UNiagaraComponent* VFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, BreakVFX, Root, FRotator::ZeroRotator, FVector::OneVector, true, false, ENCPoolMethod::AutoRelease);
	if (!VFX)
		return;

	// World Space Emitterなので、大きさ・位置はUser Parameterで渡してから開始する。
	VFX->SetVariableFloat(TEXT("User.Scale"), BreakVFXScale);
	VFX->SetVariableFloat(TEXT("User.FootprintRadius"), Footprint / FMath::Max(BreakVFXScale, .01f));
	VFX->SetVariableFloat(TEXT("User.CenterHeight"), CenterHeight / FMath::Max(BreakVFXScale, .01f));
	VFX->SetVariableInt(TEXT("User.FireCount"), BreakFireCount);
	VFX->SetVariableInt(TEXT("User.SmokeCount"), BreakSmokeCount);
	VFX->SetVariableInt(TEXT("User.SparkCount"), BreakSparkCount);
	VFX->SetVariableInt(TEXT("User.DebrisCount"), BreakDebrisCount);
	VFX->SetVariableInt(TEXT("User.DustCount"), BreakDustCount);
	VFX->SetVariableLinearColor(TEXT("User.FlashColor"), BreakFlashColor);
	VFX->SetVariableLinearColor(TEXT("User.LightColor"), BreakLightColor);
	VFX->SetVariableFloat(TEXT("User.LightIntensity"), BreakLightIntensity);
	VFX->SetVariableFloat(TEXT("User.LightRadius"), BreakLightRadius);
	VFX->SetVariableLinearColor(TEXT("User.SmokeTint"), BreakSmokeTint);
	VFX->SetVariableLinearColor(TEXT("User.DustColor"), BreakDustColor);
	VFX->Activate(true);
	++BreakVFXSpawnCount;
	UE_LOG(LogCageVisual, Log, TEXT("[CageVFX] %s 破壊VFX 再生 at %s (足元半径 %.0f / 中心高さ %.0f / 準備済み %d)"),
		*GetNameSafe(GetOwner()), *Root.ToCompactString(), Footprint, CenterHeight, BreakVFX->IsReadyToRun() ? 1 : 0);

	// 爆発の揺れ（演出のみ）。檻から離れているほど弱く、範囲外では出さない。押し出しの向きは檻→カメラ。
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	UACFCameraSubsystem* Cameras = PC && PC->IsLocalController() ? UACFCameraBlueprintLibrary::GetCameraSubsystem(PC) : nullptr;
	if (BreakCameraEffect && Cameras && Cameras->CanPlayCameraEffects() && PC->PlayerCameraManager)
	{
		const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
		const float Falloff = 1.f - FMath::Clamp(static_cast<float>(FVector::Dist(CameraLocation, Root)) / FMath::Max(BreakCameraEffectRadius, 1.f), 0.f, 1.f);
		if (Falloff > 0.f && BreakCameraEffectScale > 0.f)
		{
			FACFCameraEffectParams Params;
			Params.Scale = BreakCameraEffectScale * Falloff;
			Params.bUseSourceLocation = true;
			Params.SourceLocation = Root;
			Params.Direction = (CameraLocation - Root).GetSafeNormal2D();
			// このComponentが破棄されたらACF側でも止まる。短い単発Effectなので、ハンドルは保持しない。
			Params.Instigator = this;
			Cameras->PlayCameraEffect(BreakCameraEffect, Params);
		}
	}
}
