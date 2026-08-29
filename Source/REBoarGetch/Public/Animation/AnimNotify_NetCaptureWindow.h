#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_NetCaptureWindow.generated.h"

/** 網Montage上で捕獲Collisionの有効・無効を切り替えるAnimNotifyです。 */
UCLASS()
class REBOARGETCH_API UAnimNotify_NetCaptureWindow : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** Meshの所有Characterが装備する網へ、捕獲判定窓の切替を通知します。 */
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	/** 判定窓の開始・終了がMontage上で識別できる表示名を返します。 */
	virtual FString GetNotifyName_Implementation() const override;

protected:
	/** trueで捕獲判定ON、falseでOFFのNotifyとして使います。 */
	UPROPERTY(EditAnywhere, Category = "Net")
	bool bEnableCaptureWindow = true;
};
