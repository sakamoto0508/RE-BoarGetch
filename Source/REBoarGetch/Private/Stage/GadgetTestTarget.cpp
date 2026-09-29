#include "Stage/GadgetTestTarget.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
AGadgetTestTarget::AGadgetTestTarget()
{
    PrimaryActorTick.bCanEverTick=false;
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Target"));SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cube"));Mesh->SetStaticMesh(Shape.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/BP/Cage/Materials/M_Cage_Glow"));Mesh->SetMaterial(0,Mat.Object);
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));Mesh->SetGenerateOverlapEvents(true);Mesh->SetCanEverAffectNavigation(false);
}
void AGadgetTestTarget::ActivateByAttack_Implementation(AActor*,AActor*)
{
    Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/BP/Cage/Materials/M_Cage_Yellow")));
    GetWorldTimerManager().SetTimer(ResetTimer,this,&AGadgetTestTarget::ResetColor,.6f,false);
}
void AGadgetTestTarget::ResetColor(){Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/BP/Cage/Materials/M_Cage_Glow")));}
void AGadgetTestTarget::EndPlay(const EEndPlayReason::Type Reason){GetWorldTimerManager().ClearTimer(ResetTimer);Super::EndPlay(Reason);}
