#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_NetCaptureWindow.generated.h"

UCLASS()
class REBOARGETCH_API UAnimNotify_NetCaptureWindow : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

protected:
	/** trueで捕獲判定ON、falseでOFFのNotifyとして使います。 */
	UPROPERTY(EditAnywhere, Category = "Net")
	bool bEnableCaptureWindow = true;
};
