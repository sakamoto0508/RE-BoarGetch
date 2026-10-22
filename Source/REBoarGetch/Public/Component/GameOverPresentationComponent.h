#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameOverPresentationComponent.generated.h"

class AGameOverPresentationDirector;

UENUM(BlueprintType)
enum class EGameOverPresentationPhase : uint8
{
 Idle,
 /** 演出BP（DirectorClass）が再生中です。 */
 Playing,
 /** 完了通知済み。GameOver UIが表示されています。 */
 Finished
};

/**
 * GameOver確定（HP0）後、GameOver UIを出すまでのダウン演出の進行制御です（演出の内容はBP）。
 * DirectorClass（BP_GameOverPresentation）を生成して開始し、完了報告またはSafetyTimeout（実時間）で
 * GameModeのCompleteStageEndPresentationへ戻します。演出BPが未設定・生成失敗なら開始せず、従来どおり即時UIを表示します。
 * GameOver判定・入力/AI停止・UI表示の可否はGameModeが所有し、このComponentとBPは関与しません。Clearでは使用しません。
 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UGameOverPresentationComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UGameOverPresentationComponent();
 /** GameModeから1回だけ呼びます。開始できない場合はfalseを返し、呼び出し側は従来どおり即時完了します。 */
 bool StartPresentation();
 /** 再生中の演出を中断し、演出BPを破棄します（時間倍率・カメラ等は演出BPのEndPlayで復元されます）。 */
 void CancelPresentation();
 /** GameOver UI表示時にControllerから呼び、演出BPへ通知します（敗北ジングル、警告ラインを控えめに残す）。 */
 void NotifyGameOverUIShown();
 UFUNCTION(BlueprintPure, Category="GameOver Presentation") bool IsPresentationActive() const { return Phase == EGameOverPresentationPhase::Playing; }
 UFUNCTION(BlueprintPure, Category="GameOver Presentation") EGameOverPresentationPhase GetPhase() const { return Phase; }
 /** 演出開始からの経過秒数（実時間）。 */
 UFUNCTION(BlueprintPure, Category="GameOver Presentation") float GetPresentationAge() const;
 UFUNCTION(BlueprintPure, Category="GameOver Presentation") AGameOverPresentationDirector* GetDirector() const { return Director; }

 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation", meta=(ToolTip="OFFの場合は演出を行わず、従来どおりGameOver確定後すぐUIを表示します。")) bool bEnabled = true;
 /** 演出の内容（順序・タイミング・Montage・カメラ・警告UI・SE）を持つBP。未設定なら演出なしで即UIへ進みます。 */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation") TSubclassOf<AGameOverPresentationDirector> DirectorClass;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation", meta=(ClampMin="1", ToolTip="演出BPが完了報告しない場合でもUIへ進む上限時間（実時間・秒）。")) float SafetyTimeout = 6.f;

protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
 void Finish();
 EGameOverPresentationPhase Phase = EGameOverPresentationPhase::Idle;
 double StartRealTime = 0.0;
 UPROPERTY(Transient) TObjectPtr<AGameOverPresentationDirector> Director;
};
