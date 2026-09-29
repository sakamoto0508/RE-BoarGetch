#pragma once
#include "CoreMinimal.h"
#include "Boar/BoarBase.h"
#include "NetPracticeBoar.generated.h"
/** Dedicated capture dummy. Uses the real capture event, but never awards stage progress. */
UCLASS()
class REBOARGETCH_API ANetPracticeBoar : public ABoarBase
{
 GENERATED_BODY()
public:
 ANetPracticeBoar();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere, Category="Practice",meta=(ClampMin="0.5")) float ResetDelay=2.f;
private:
 UFUNCTION() void Captured(AActor* Capturer);
 void ResetDummy();
 FTimerHandle ResetTimer;
 UPROPERTY() TObjectPtr<class UTextRenderComponent> Feedback;
};
