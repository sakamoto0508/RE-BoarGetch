#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_BoarFootstep.generated.h"

/**
 * 歩行・走行アニメーションの接地フレームに置く足音Notifyです。
 * 音源はNotifyごとに持たず、所有者の設定（PlayerはBP_BoarPlayerCharacterのPlayer Audio、Boarは種別DataAssetのAudio）を参照します。
 * そのため足音の差し替えは1か所で済み、Notifyの位置だけがAnimationに残ります。空中・捕獲済み・未設定では鳴らしません。
 */
UCLASS(meta = (DisplayName = "Boar Footstep"))
class REBOARGETCH_API UAnimNotify_BoarFootstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

protected:
	/** 発音位置にする足のBoneです。空またはBoneが無い場合はActorの足元（Capsule下端）で鳴らします。 */
	UPROPERTY(EditAnywhere, Category = "Footstep")
	FName FootBone;

	/** 所有者設定の足音音量に掛ける倍率です（左右差・走り／歩きの差の調整用）。 */
	UPROPERTY(EditAnywhere, Category = "Footstep", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;
};
