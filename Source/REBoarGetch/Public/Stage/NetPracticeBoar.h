#pragma once
#include "CoreMinimal.h"
#include "Boar/BoarBase.h"
#include "NetPracticeBoar.generated.h"
/** 捕獲練習用のダミー。本物の捕獲イベントを使うが、ステージ進行には一切加算しない。 */
UCLASS()
class REBOARGETCH_API ANetPracticeBoar : public ABoarBase
{
 GENERATED_BODY()

public:
 ANetPracticeBoar();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;

 /** 捕獲後に練習用Boarを再表示するまでの待機時間（秒）。 */
 UPROPERTY(EditAnywhere, Category="Practice",meta=(ClampMin="0.5"))
 float ResetDelay=2.f;

private:
 UFUNCTION()
 void Captured(AActor* Capturer);

 void ResetDummy();
 /** 練習用Boarの再表示予約を管理するタイマー。 */
 FTimerHandle ResetTimer;

 /** 練習用の捕獲フィードバックを表示するコンポーネント。 */
 UPROPERTY()
 TObjectPtr<class UTextRenderComponent> Feedback;
};
