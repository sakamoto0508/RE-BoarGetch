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

 UPROPERTY(EditAnywhere, Category="Practice",meta=(ClampMin="0.5"))
 float ResetDelay=2.f;

private:
 UFUNCTION()
 void Captured(AActor* Capturer);

 void ResetDummy();
 FTimerHandle ResetTimer;

 UPROPERTY()
 TObjectPtr<class UTextRenderComponent> Feedback;
};
