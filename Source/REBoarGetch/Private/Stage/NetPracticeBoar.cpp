#include "Stage/NetPracticeBoar.h"
#include "Component/CaptureComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
ANetPracticeBoar::ANetPracticeBoar()
{
 AutoPossessAI=EAutoPossessAI::Disabled;
 GetCapsuleComponent()->SetCapsuleSize(58,70);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/Lobby/Materials/MI_Lobby_Ice"));
 auto Add=[&](FName Name,FVector P,FVector S)
 {
  auto* C=CreateDefaultSubobject<UStaticMeshComponent>(Name);C->SetupAttachment(GetRootComponent());C->SetStaticMesh(Cube.Object);C->SetMaterial(0,Mat.Object);
  C->SetRelativeLocation(P);C->SetRelativeScale3D(S);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);
 };
 Add(TEXT("Body"),FVector(0,0,0),FVector(1.5,.8,.8));Add(TEXT("Snout"),FVector(87,0,-10),FVector(.35,.5,.35));
 Add(TEXT("EarLeft"),FVector(45,-26,45),FVector(.25,.15,.4));Add(TEXT("EarRight"),FVector(45,26,45),FVector(.25,.15,.4));
 Feedback=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Feedback"));Feedback->SetupAttachment(GetRootComponent());Feedback->SetRelativeLocation(FVector(0,0,125));Feedback->SetWorldSize(24);Feedback->SetHorizontalAlignment(EHTA_Center);Feedback->SetText(FText::FromString(TEXT("NET TARGET")));
}
void ANetPracticeBoar::BeginPlay()
{
 Super::BeginPlay();GetCharacterMovement()->DisableMovement();
 if(auto* C=FindComponentByClass<UCaptureComponent>())C->OnCaptured.AddUniqueDynamic(this,&ANetPracticeBoar::Captured);
}
void ANetPracticeBoar::Captured(AActor*)
{
 Feedback->SetText(FText::FromString(TEXT("CAPTURE OK")));Feedback->SetTextRenderColor(FColor::Yellow);
 GetWorldTimerManager().SetTimer(ResetTimer,this,&ANetPracticeBoar::ResetDummy,FMath::Max(.5f,ResetDelay),false);
}
void ANetPracticeBoar::ResetDummy()
{
 if(auto* C=FindComponentByClass<UCaptureComponent>())C->Release();
 GetCharacterMovement()->DisableMovement();Feedback->SetText(FText::FromString(TEXT("NET TARGET")));Feedback->SetTextRenderColor(FColor::Cyan);
}
void ANetPracticeBoar::EndPlay(const EEndPlayReason::Type Reason)
{
 GetWorldTimerManager().ClearTimer(ResetTimer);Super::EndPlay(Reason);
}
