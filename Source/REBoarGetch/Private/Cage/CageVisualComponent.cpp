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
