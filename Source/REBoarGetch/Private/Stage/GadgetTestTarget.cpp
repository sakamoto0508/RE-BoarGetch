#include "Stage/GadgetTestTarget.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

namespace GadgetTestTarget
{
	/** 通常時と、当たった直後に表示するMaterial。 */
	const TCHAR* const IdleMaterialPath = TEXT("/Game/Art/Materials/Cage/M_Cage_Glow");
	const TCHAR* const HitMaterialPath = TEXT("/Game/Art/Materials/Cage/M_Cage_Yellow");
}

AGadgetTestTarget::AGadgetTestTarget()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Target"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cube"));
	Mesh->SetStaticMesh(Shape.Object);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(GadgetTestTarget::IdleMaterialPath);
	Mesh->SetMaterial(0, Mat.Object);

	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->SetGenerateOverlapEvents(true);
	Mesh->SetCanEverAffectNavigation(false);
}

void AGadgetTestTarget::ActivateByAttack_Implementation(AActor*, AActor*)
{
	Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, GadgetTestTarget::HitMaterialPath));
	GetWorldTimerManager().SetTimer(ResetTimer, this, &AGadgetTestTarget::ResetColor, HitColorDuration, false);
}

void AGadgetTestTarget::ResetColor()
{
	Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, GadgetTestTarget::IdleMaterialPath));
}

void AGadgetTestTarget::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(ResetTimer);
	Super::EndPlay(Reason);
}
