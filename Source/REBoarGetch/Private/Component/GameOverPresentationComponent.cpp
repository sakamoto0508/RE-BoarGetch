#include "Component/GameOverPresentationComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "Player/BoarPlayerController.h"
#include "Presentation/GameOverPresentationDirector.h"

UGameOverPresentationComponent::UGameOverPresentationComponent()
{
 PrimaryComponentTick.bCanEverTick = true;
 PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UGameOverPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 CancelPresentation();
 Super::EndPlay(Reason);
}

float UGameOverPresentationComponent::GetPresentationAge() const
{
 return Phase == EGameOverPresentationPhase::Idle || !GetWorld() ? 0.f : float(GetWorld()->GetRealTimeSeconds() - StartRealTime);
}

bool UGameOverPresentationComponent::StartPresentation()
{
 // 多重起動防止：1回のGameOverで開始できるのは1度だけです。演出BP未設定なら呼び出し側が即時完了します。
 if (!bEnabled || !DirectorClass || Phase != EGameOverPresentationPhase::Idle) return false;
 auto* PC = Cast<ABoarPlayerController>(GetOwner());
 if (!PC || !PC->IsLocalController()) return false;
 FActorSpawnParameters Params;
 Params.ObjectFlags |= RF_Transient;
 Params.Owner = PC;
 Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 Director = GetWorld()->SpawnActor<AGameOverPresentationDirector>(DirectorClass, FTransform::Identity, Params);
 if (!Director) return false;
 Phase = EGameOverPresentationPhase::Playing;
 StartRealTime = GetWorld()->GetRealTimeSeconds();
 SetComponentTickEnabled(true);
 UE_LOG(LogTemp, Log, TEXT("[GameOver] Presentation start (%s)"), *GetNameSafe(DirectorClass));
 Director->BeginPresentation(PC, FSimpleDelegate::CreateUObject(this, &UGameOverPresentationComponent::Finish));
 return true;
}

void UGameOverPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
 // 演出BPが完了報告しない・破棄された場合も必ずUIへ進めます（ヒットストップの影響を受けない実時間）。
 if (Phase == EGameOverPresentationPhase::Playing && (GetPresentationAge() >= SafetyTimeout || !IsValid(Director)))
 {
  UE_LOG(LogTemp, Warning, TEXT("[GameOver] Presentation timed out or director lost; showing GameOver UI."));
  Finish();
 }
}

void UGameOverPresentationComponent::Finish()
{
 if (Phase != EGameOverPresentationPhase::Playing) return;
 Phase = EGameOverPresentationPhase::Finished;
 SetComponentTickEnabled(false);
 UE_LOG(LogTemp, Log, TEXT("[GameOver] Presentation finished after %.2fs"), GetPresentationAge());
 // 演出BPはUI表示中も残し、倒れた姿勢・カメラ・警告ラインを保持します（Level遷移で破棄）。
 if (auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr) Mode->CompleteStageEndPresentation();
}

void UGameOverPresentationComponent::NotifyGameOverUIShown()
{
 if (IsValid(Director)) Director->OnGameOverUIShown();
}

void UGameOverPresentationComponent::CancelPresentation()
{
 if (Phase == EGameOverPresentationPhase::Idle) return;
 SetComponentTickEnabled(false);
 if (IsValid(Director)) Director->Destroy();
 Director = nullptr;
 Phase = EGameOverPresentationPhase::Idle;
}
