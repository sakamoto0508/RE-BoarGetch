#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StageClearPresentationComponent.generated.h"

class AStageClearPresentationDirector;

UENUM(BlueprintType)
enum class EStageClearPresentationPhase : uint8
{
 Idle,
 /** 最後の捕獲演出の完了を待っています（Gameplay側の進行制御）。 */
 WaitingCapture,
 /** 演出BP（DirectorClass）が再生中です。 */
 Playing,
 /** 完了通知済み。Result表示へ進みました。 */
 Finished
};

/**
 * ステージクリア確定後、Result表示までの勝利演出の進行制御です（演出の内容はBP）。
 * 捕獲演出の完了待ち → DirectorClass（BP_StageClearPresentation）を生成して開始 → 完了報告またはSafetyTimeoutで
 * GameModeのCompleteStageEndPresentationへ戻します。演出BPが未設定・生成失敗の場合は開始せず、従来どおり即時Resultへ進みます。
 * Clear判定・保存・Result表示の可否はGameModeが所有し、このComponentとBPは関与しません。GameOverでは使用しません。
 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UStageClearPresentationComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UStageClearPresentationComponent();
 /** GameModeから1回だけ呼びます。開始できない場合はfalseを返し、呼び出し側は従来どおり即時完了します。 */
 bool StartPresentation();
 /** 再生中の演出を中断し、演出BPを破棄します（見た目はBP側EndPlayで復元されます）。 */
 void CancelPresentation();
 /** Result表示時にControllerから呼び、演出BPへ通知します（Result BGMなど）。 */
 void NotifyResultShown();
 UFUNCTION(BlueprintPure, Category="Stage Clear Presentation") bool IsPresentationActive() const { return Phase == EStageClearPresentationPhase::WaitingCapture || Phase == EStageClearPresentationPhase::Playing; }
 UFUNCTION(BlueprintPure, Category="Stage Clear Presentation") EStageClearPresentationPhase GetPhase() const { return Phase; }
 UFUNCTION(BlueprintPure, Category="Stage Clear Presentation") AStageClearPresentationDirector* GetDirector() const { return Director; }

 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation", meta=(ToolTip="OFFの場合は演出を行わず、従来どおりClear確定後すぐResultへ進みます。")) bool bEnabled = true;
 /** 演出の内容（順序・タイミング・Montage・VFX・SE・カメラ）を持つBP。未設定なら演出なしで即Resultへ進みます。 */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation") TSubclassOf<AStageClearPresentationDirector> DirectorClass;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation", meta=(ClampMin="0", ToolTip="最後の捕獲演出の完了を待つ最大時間（秒）。超えた場合は捕獲演出を中断して勝利演出へ進みます。")) float CaptureWaitTimeout = 2.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation", meta=(ClampMin="3", ToolTip="演出BPが完了報告しない場合でもResultへ進む上限時間（秒）。無限待機を防ぎます。")) float SafetyTimeout = 14.f;

protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
 void BeginDirector();
 void Finish();
 EStageClearPresentationPhase Phase = EStageClearPresentationPhase::Idle;
 float PhaseAge = 0.f, TotalAge = 0.f;
 UPROPERTY(Transient) TObjectPtr<AStageClearPresentationDirector> Director;
};
