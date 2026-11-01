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

UCageVisualComponent::UCageVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .05f;
	CageMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CageMesh"));
	CageMeshComponent->SetupAttachment(this);
	CageMeshComponent->bUseDefaultCollision = false;
	CageMeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
	CageMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CageMeshComponent->SetCanEverAffectNavigation(false);
	CageMeshComponent->SetGenerateOverlapEvents(false);
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
	if (!CageMeshComponent->IsRegistered()) CageMeshComponent->RegisterComponent();
	ApplyVisualSource();
	Gauge->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	// 代替Widgetは使わない。未設定ならWidgetを生成せず、BeginPlayでErrorを出す。
	Gauge->SetWidgetClass(GaugeWidgetClass);
	Gauge->SetDrawSize(GaugeDrawSize);
	Gauge->SetRelativeLocation(FVector(0, 0, GaugeHeight));
	Gauge->SetRelativeScale3D(FVector(GaugeWorldScale));
	if (!Gauge->IsRegistered()) Gauge->RegisterComponent();
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
		UE_LOG(LogTemp, Error, TEXT("[CageVisual] %s.%s: CageMesh (SM_Cage) is missing; skipping the cage visual and lamps (HP and destruction keep working)."),
		       *GetNameSafe(GetOwner()), *GetName());
	else
		for (int32 Slot : LampMaterialSlots) if (Slot >= 0 && Slot < CageMeshComponent->GetNumMaterials()) Lamps.Add(
			CageMeshComponent->CreateDynamicMaterialInstance(Slot));
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
	FString Missing;
	// 基底C++クラスはレイアウトを持たないため、WBP（非Native）の指定を必須にする。
	if (!GaugeWidgetClass || GaugeWidgetClass->HasAnyClassFlags(CLASS_Native))
		Missing = TEXT("GaugeWidgetClass (WBP_CageHealth)");
	else
	{
		Gauge->InitWidget();
		const auto* Widget = Cast<UCageHealthWidget>(Gauge->GetUserWidgetObject());
		if (!Widget)
			Missing = FString::Printf(TEXT("Widget instance of %s"), *GaugeWidgetClass->GetName());
		else if (!Widget->HasRequiredBindings())
			Missing = FString::Printf(TEXT("%s.HealthBar (ProgressBar binding)"), *GaugeWidgetClass->GetName());
	}
	if (!Missing.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[CageVisual] %s.%s: %s is missing; skipping the HP gauge (HP and destruction keep working)."),
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
	float Next = FMath::Clamp(Hp / FMath::Max(MaxHp, 1.f), 0.f, 1.f);
	if (Next < Fraction) FlashRemaining = .18f;
	Fraction = Next;
	if (Fraction > Trail) Trail = Fraction;
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
	if (!Cage || bDestroyed)return;
	auto* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera)return;
	if (bGaugeAvailable)
	{
		const FVector Delta = Camera->GetCameraLocation() - Gauge->GetComponentLocation();
		const bool bGaugeVisible = DisplayDistance <= 0 || Delta.SizeSquared() <= FMath::Square(DisplayDistance);
		Gauge->SetVisibility(bGaugeVisible);
		if (bGaugeVisible)Gauge->SetWorldRotation(Delta.Rotation());
	}
	const bool bAnimating = FlashRemaining > 0 || Trail > Fraction + .001f || Fraction <= LowThreshold;
	FlashRemaining = FMath::Max(0.f, FlashRemaining - Dt);
	Trail = FMath::FInterpConstantTo(Trail, Fraction, Dt, .75f);
	if (!bRedraw && !bAnimating)return;
	const bool bLow = Fraction > 0 && Fraction <= LowThreshold;
	const float Pulse = bLow ? .5f + .5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 4.f) : 0.f;
	const FLinearColor Color = bLow ? GaugeLowColor : Fraction <= MediumThreshold ? GaugeMediumColor : GaugeHighColor;
	const float Flash = FlashRemaining / .18f;
	if (bGaugeAvailable)
	{
		if (auto* W = Cast<UCageHealthWidget>(Gauge->GetUserWidgetObject()))W->SetPresentation(
			Fraction, Trail, Color, Flash, Pulse);
		Gauge->RequestRedraw();
	}
	for (UMaterialInstanceDynamic* M : Lamps)if (M)
	{
		M->SetVectorParameterValue(TEXT("Tint"), FLinearColor::LerpUsingHSV(
			                           bLow ? LampLowTint : LampNormalTint,
			                           FLinearColor::White, Flash));
		M->SetScalarParameterValue(TEXT("Intensity"), bLow ? .5f + Pulse * .3f : 1.2f);
	}
	bRedraw = false;
}
