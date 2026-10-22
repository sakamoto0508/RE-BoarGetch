#include "Component/StageClearPresentationComponent.h"
#include "Component/CapturePresentationComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "Player/BoarPlayerController.h"
#include "Presentation/StageClearPresentationDirector.h"

UStageClearPresentationComponent::UStageClearPresentationComponent()
{
 PrimaryComponentTick.bCanEverTick = true;
 PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UStageClearPresentationComponent::BeginPlay()
{
 Super::BeginPlay();
 SetComponentTickEnabled(false);
}

void UStageClearPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 CancelPresentation();
 Super::EndPlay(Reason);
}

bool UStageClearPresentationComponent::StartPresentation()
{
 // 多重起動防止：1回のStageで開始できるのは1度だけです。演出BP未設定なら呼び出し側が即時完了します。
 if (!bEnabled || !DirectorClass || Phase != EStageClearPresentationPhase::Idle) return false;
 const auto* PC = Cast<ABoarPlayerController>(GetOwner());
 if (!PC || !PC->IsLocalController() || !PC->GetPawn()) return false;
 Phase = EStageClearPresentationPhase::WaitingCapture;
 PhaseAge = TotalAge = 0.f;
 SetComponentTickEnabled(true);
 UE_LOG(LogTemp, Log, TEXT("[StageClear] Presentation requested"));
 return true;
}

void UStageClearPresentationComponent::BeginDirector()
{
 auto* PC = Cast<ABoarPlayerController>(GetOwner());
 FActorSpawnParameters Params;
 Params.ObjectFlags |= RF_Transient;
 Params.Owner = PC;
 Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 Director = PC ? GetWorld()->SpawnActor<AStageClearPresentationDirector>(DirectorClass, FTransform::Identity, Params) : nullptr;
 if (!Director)
 {
  UE_LOG(LogTemp, Warning, TEXT("[StageClear] Presentation director could not be spawned; showing Result."));
  Finish();
  return;
 }
 Phase = EStageClearPresentationPhase::Playing;
 PhaseAge = 0.f;
 Director->BeginPresentation(PC, FSimpleDelegate::CreateUObject(this, &UStageClearPresentationComponent::Finish));
}

void UStageClearPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
 const float Dt = FMath::Min(DeltaTime, .1f);
 TotalAge += Dt; PhaseAge += Dt;
 if (Phase == EStageClearPresentationPhase::WaitingCapture)
 {
  const auto* PC = Cast<ABoarPlayerController>(GetOwner());
  UCapturePresentationComponent* Capture = PC ? PC->CapturePresentation.Get() : nullptr;
  if (Capture && Capture->HasPendingCaptures() && PhaseAge < CaptureWaitTimeout) return;
  if (Capture && Capture->HasPendingCaptures()) Capture->CancelPresentation();
  BeginDirector();
  return;
 }
 // 演出BPが完了報告しない・Playerが消えた場合も、残りを省略してResultへ進めます。
 if (Phase == EStageClearPresentationPhase::Playing && (TotalAge > SafetyTimeout || !IsValid(Director)))
 {
  UE_LOG(LogTemp, Warning, TEXT("[StageClear] Presentation timed out or director lost; showing Result."));
  Finish();
 }
}

void UStageClearPresentationComponent::Finish()
{
 if (Phase != EStageClearPresentationPhase::WaitingCapture && Phase != EStageClearPresentationPhase::Playing) return;
 Phase = EStageClearPresentationPhase::Finished;
 SetComponentTickEnabled(false);
 UE_LOG(LogTemp, Log, TEXT("[StageClear] Presentation finished after %.2fs"), TotalAge);
 // 演出BPはResult表示中も残し、勝利カメラ・消失状態を保持します（Level遷移で破棄）。
 if (auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr) Mode->CompleteStageEndPresentation();
}

void UStageClearPresentationComponent::NotifyResultShown()
{
 if (IsValid(Director)) Director->OnResultShown();
}

void UStageClearPresentationComponent::CancelPresentation()
{
 if (Phase == EStageClearPresentationPhase::Idle) return;
 SetComponentTickEnabled(false);
 if (IsValid(Director)) Director->Destroy();
 Director = nullptr;
 Phase = EStageClearPresentationPhase::Idle;
}
