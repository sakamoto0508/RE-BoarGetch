#include "Cage/CageVisualComponent.h"
#include "Cage/Cage.h"
#include "UI/CageHealthWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

UCageVisualComponent::UCageVisualComponent()
{
	PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickInterval=.05f;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> White(TEXT("/Game/BP/Cage/Materials/M_Cage_White"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Navy(TEXT("/Game/BP/Cage/Materials/M_Cage_Navy"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Yellow(TEXT("/Game/BP/Cage/Materials/M_Cage_Yellow"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/BP/Cage/Materials/M_Cage_Glow"));
	int32 Index=0;
	auto Box=[&](FVector P,FVector Size,UMaterialInterface* Material,bool bLamp=false)
	{
		auto* C=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Shell_%02d"),Index++));
		C->SetupAttachment(this); C->SetStaticMesh(Cube.Object); C->SetRelativeLocation(P); C->SetRelativeScale3D(Size/100.f);
		C->bUseDefaultCollision=false; C->SetCollisionProfileName(TEXT("NoCollision")); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCanEverAffectNavigation(false); C->SetGenerateOverlapEvents(false);
		C->SetMaterial(0,Material); if(bLamp) C->ComponentTags.Add(TEXT("CageLamp")); Shell.Add(C);
	};
	Box(FVector(0,0,14),FVector(550,550,24),Navy.Object);
	for(int32 X:{-1,1}) for(int32 Y:{-1,1})
	{
		Box(FVector(X*238,Y*238,273),FVector(48,48,500),White.Object);
		for(float Z:{40.f,521.f}) Box(FVector(X*238,Y*238,Z),FVector(66,66,42),Yellow.Object);
		Box(FVector(X*264,Y*238,280),FVector(7,20,200),Glow.Object,true);
	}
	for(int32 S:{-1,1}) for(float Z:{46.f,514.f})
	{
		Box(FVector(S*239,0,Z),FVector(43,475,35),White.Object);
		Box(FVector(0,S*239,Z),FVector(475,43,35),White.Object);
	}
	Box(FVector(271,0,466),FVector(16,180,56),Navy.Object);
	Box(FVector(282,0,466),FVector(7,145,26),Glow.Object,true);
	Gauge=CreateDefaultSubobject<UWidgetComponent>(TEXT("CageHPGauge")); Gauge->SetupAttachment(this);
	Gauge->SetWidgetSpace(EWidgetSpace::World); Gauge->SetDrawSize(FVector2D(640,100)); Gauge->SetPivot(FVector2D(.5,.5)); Gauge->SetWidgetClass(UCageHealthWidget::StaticClass());
	Gauge->SetCollisionEnabled(ECollisionEnabled::NoCollision); Gauge->SetCanEverAffectNavigation(false); Gauge->SetGenerateOverlapEvents(false); Gauge->SetTwoSided(true); Gauge->SetManuallyRedraw(true); Gauge->SetTickWhenOffscreen(false);
}
void UCageVisualComponent::OnRegister()
{
	Super::OnRegister();
	for(UStaticMeshComponent* C:Shell) if(C)
	{
		C->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		if(!C->IsRegistered()) C->RegisterComponent();
		C->UpdateComponentToWorld();
	}
	Gauge->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	Gauge->SetRelativeLocation(FVector(0,0,GaugeHeight)); Gauge->SetRelativeScale3D(FVector(GaugeWorldScale));
	if(!Gauge->IsRegistered()) Gauge->RegisterComponent();
}
void UCageVisualComponent::BeginPlay()
{
	Super::BeginPlay(); Cage=Cast<ACage>(GetOwner());
	if(!Cage){SetComponentTickEnabled(false);return;}
	for(UStaticMeshComponent* C:Shell) if(C->ComponentHasTag(TEXT("CageLamp"))) Lamps.Add(C->CreateDynamicMaterialInstance(0));
	Gauge->InitWidget(); Fraction=FMath::Clamp(Cage->GetHP()/FMath::Max(Cage->GetMaxHP(),1.f),0.f,1.f); Trail=Fraction;
	Cage->OnHealthChanged.AddUniqueDynamic(this,&UCageVisualComponent::HealthChanged);
	Cage->OnCageDestroyed.AddUniqueDynamic(this,&UCageVisualComponent::Destroyed);
	Cage->OnRespawned.AddUniqueDynamic(this,&UCageVisualComponent::Respawned);
	bDestroyed=Cage->GetIsCageDestroyed();
}
void UCageVisualComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if(Cage){Cage->OnHealthChanged.RemoveDynamic(this,&UCageVisualComponent::HealthChanged);Cage->OnCageDestroyed.RemoveDynamic(this,&UCageVisualComponent::Destroyed);Cage->OnRespawned.RemoveDynamic(this,&UCageVisualComponent::Respawned);}
	Super::EndPlay(Reason);
}
void UCageVisualComponent::HealthChanged(float Hp,float MaxHp)
{
	float Next=FMath::Clamp(Hp/FMath::Max(MaxHp,1.f),0.f,1.f); if(Next<Fraction) FlashRemaining=.18f;
	Fraction=Next; if(Fraction>Trail) Trail=Fraction; bRedraw=true;
}
void UCageVisualComponent::Destroyed(){bDestroyed=true;FlashRemaining=0;Gauge->SetVisibility(false);}
void UCageVisualComponent::Respawned(){bDestroyed=false;Trail=Fraction;FlashRemaining=0;bRedraw=true;}
void UCageVisualComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt,Type,TickFunction);
	if(!Cage||bDestroyed)return;
	auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0); if(!Camera)return;
	const FVector Delta=Camera->GetCameraLocation()-Gauge->GetComponentLocation();
	const bool bGaugeVisible=DisplayDistance<=0||Delta.SizeSquared()<=FMath::Square(DisplayDistance);
	Gauge->SetVisibility(bGaugeVisible); if(bGaugeVisible)Gauge->SetWorldRotation(Delta.Rotation());
	const bool bAnimating=FlashRemaining>0||Trail>Fraction+.001f||Fraction<=LowThreshold;
	FlashRemaining=FMath::Max(0.f,FlashRemaining-Dt);Trail=FMath::FInterpConstantTo(Trail,Fraction,Dt,.75f);
	if(!bRedraw&&!bAnimating)return;
	const bool bLow=Fraction>0&&Fraction<=LowThreshold;
	const float Pulse=bLow?.5f+.5f*FMath::Sin(GetWorld()->GetTimeSeconds()*4.f):0.f;
	const FLinearColor Color=bLow?FLinearColor(1,.035f,.02f):Fraction<=MediumThreshold?FLinearColor(1,.72f,.025f):FLinearColor(.015f,.75f,1);
	const float Flash=FlashRemaining/.18f;
	if(auto* W=Cast<UCageHealthWidget>(Gauge->GetUserWidgetObject()))W->SetPresentation(Fraction,Trail,Color,Flash,Pulse);
	Gauge->RequestRedraw();
	for(UMaterialInstanceDynamic* M:Lamps)if(M){M->SetVectorParameterValue(TEXT("Tint"),FLinearColor::LerpUsingHSV(bLow?FLinearColor(1,.04f,.015f):FLinearColor(.01f,.65f,1),FLinearColor::White,Flash));M->SetScalarParameterValue(TEXT("Intensity"),bLow?.5f+Pulse*.3f:1.2f);}
	bRedraw=false;
}

