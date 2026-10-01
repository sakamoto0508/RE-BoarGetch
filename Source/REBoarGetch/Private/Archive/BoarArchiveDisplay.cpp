#include "Archive/BoarArchiveDisplay.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Boar/BoarBase.h"
#include "Stage/StageConfig.h"
#include "Core/BoarGameInstance.h"

ABoarArchiveDisplay::ABoarArchiveDisplay()
{
    PrimaryActorTick.bCanEverTick=true;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));SetRootComponent(SceneRoot);
    BoarVisual=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BoarVisual"));BoarVisual->SetupAttachment(SceneRoot);
    BoarVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoarVisual->SetGenerateOverlapEvents(false);BoarVisual->SetCanEverAffectNavigation(false);
    BoarVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
}
void ABoarArchiveDisplay::ConfigureVisual(USkeletalMesh* Mesh,const TArray<UMaterialInterface*>& Materials,UAnimSequence* Idle)
{
    BoarVisual->SetSkeletalMesh(Mesh);
    BoarVisual->EmptyOverrideMaterials();
    for(int32 I=0;I<Materials.Num();++I) if(Materials[I])BoarVisual->SetMaterial(I,Materials[I]);
    BoarVisual->SetRelativeLocation(VisualOffset);BoarVisual->SetRelativeScale3D(FVector(DisplayScale));
    BoarVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    if(Idle && Mesh && Idle->GetSkeleton()==Mesh->GetSkeleton())BoarVisual->PlayAnimation(Idle,true);
    else BoarVisual->SetAnimation(nullptr);
    BoarVisual->SetVisibility(Mesh!=nullptr);BoarVisual->SetHiddenInGame(Mesh==nullptr);
}
void ABoarArchiveDisplay::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if(!GetWorld() || GetWorld()->IsGameWorld())return;
    TArray<UMaterialInterface*> Mats;for(auto& M:VisualMaterials)Mats.Add(M);
    ConfigureVisual(bPreviewInEditor?PreviewMesh.Get():nullptr,Mats,IdleAnimation);
}
void ABoarArchiveDisplay::BeginPlay()
{
    Super::BeginPlay();
    if(auto* GI=GetGameInstance<UBoarGameInstance>())GI->OnArchiveDisplaysChanged.AddUniqueDynamic(this,&ABoarArchiveDisplay::RefreshDisplay);
    RefreshDisplay();
}
void ABoarArchiveDisplay::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* GI=GetGameInstance<UBoarGameInstance>())GI->OnArchiveDisplaysChanged.RemoveDynamic(this,&ABoarArchiveDisplay::RefreshDisplay);
    Super::EndPlay(Reason);
}
void ABoarArchiveDisplay::RefreshDisplay()
{
    auto* GI=GetGameInstance<UBoarGameInstance>();
    const TArray<FName> Ids=GI?GI->GetArchiveDisplayIds():TArray<FName>();
    const FName Id=Ids.IsValidIndex(SlotIndex)?Ids[SlotIndex]:NAME_None;
    const FBoarSpawnDefinition* Match=nullptr;
    int32 Matches=0;
    if(!Id.IsNone())for(const auto& Stage:StageCatalog)if(Stage)for(const auto& Def:Stage->BoarSpawnDefinitions)
        if(Def.BoarUniqueId==Id){Match=&Def;++Matches;}
    const ABoarBase* Default=Matches==1 && Match->BoarClass?Match->BoarClass.GetDefaultObject():nullptr;
    const auto* Source=Default?Default->GetMesh():nullptr;
    TArray<UMaterialInterface*> Mats=Source?Source->GetMaterials():TArray<UMaterialInterface*>();
    if(!VisualMaterials.IsEmpty()) {Mats.Reset();for(auto& M:VisualMaterials)Mats.Add(M);}
    ConfigureVisual(Source?Source->GetSkeletalMeshAsset():nullptr,Mats,
        Match && Matches==1 && !Match->ArchiveIdleAnimation.IsNull()?Match->ArchiveIdleAnimation.LoadSynchronous():IdleAnimation.Get());
    BoarVisual->SetRelativeRotation(FRotator::ZeroRotator);
    SetActorTickEnabled(Source && Source->GetSkeletalMeshAsset());
    if(!Id.IsNone() && !Source)UE_LOG(LogTemp,Warning,TEXT("[Archive] Slot %d: unresolved captured Boar ID %s; left empty."),SlotIndex,*Id.ToString());
}
void ABoarArchiveDisplay::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(BoarVisual->IsVisible())BoarVisual->AddLocalRotation(FRotator(0,RotationSpeed*DeltaSeconds,0));
}
