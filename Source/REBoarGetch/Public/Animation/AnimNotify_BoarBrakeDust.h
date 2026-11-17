#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_BoarBrakeDust.generated.h"

class UNiagaraSystem;

/**
 * 蹄が地面を踏み込むフレームに置き、足元の地面から土煙を出すAnimNotifyです（AKI-172）。
 * 切り返しの判断（Gameplay）とは結合せず、アニメーションの接地タイミングだけで発生します。見た目の差し替えはNiagaraとこのNotifyの設定で行います。
 * 足のBoneの真下を調べ、地面が近くに無い（空中）・捕獲済み・ステージ進行中でない場合は出しません。
 */
UCLASS(meta = (DisplayName = "Boar Brake Dust"))
class REBOARGETCH_API UAnimNotify_BoarBrakeDust : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_BoarBrakeDust();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

protected:
	/** 発生させる土煙です。粒子数・大きさ・寿命・色の既定値はNiagara側のUser Parameterで調整します。 */
	UPROPERTY(EditAnywhere, Category = "BrakeDust")
	TObjectPtr<UNiagaraSystem> DustSystem;

	/** 接地位置を取る足のBoneです（蹄の先端に近いBone）。 */
	UPROPERTY(EditAnywhere, Category = "BrakeDust")
	FName FootBone = TEXT("frontleg2");

	/** 足の真下に地面を探す距離（cm）です。これより地面が遠ければ空中とみなして出しません。 */
	UPROPERTY(EditAnywhere, Category = "BrakeDust", meta = (ClampMin = "1.0", Units = "Centimeters"))
	float MaxGroundDistance = 25.0f;

	/** 発生位置のずれ（Actorの向き基準、X=前・Y=右・Z=上、cm）です。 */
	UPROPERTY(EditAnywhere, Category = "BrakeDust")
	FVector LocationOffset = FVector::ZeroVector;

	/** 土煙全体の大きさの倍率です。 */
	UPROPERTY(EditAnywhere, Category = "BrakeDust", meta = (ClampMin = "0.0"))
	float Scale = 1.0f;

	/** 粒子数・大きさ・寿命・色をこのNotifyで上書きするかです。無効ならNiagaraのUser Parameterの既定値を使います。 */
	UPROPERTY(EditAnywhere, Category = "BrakeDust|Override")
	bool bOverrideParameters = false;

	UPROPERTY(EditAnywhere, Category = "BrakeDust|Override", meta = (EditCondition = "bOverrideParameters", ClampMin = "0"))
	int32 SpawnCount = 10;

	UPROPERTY(EditAnywhere, Category = "BrakeDust|Override", meta = (EditCondition = "bOverrideParameters", ClampMin = "0.0"))
	float SizeScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "BrakeDust|Override", meta = (EditCondition = "bOverrideParameters", ClampMin = "0.05", Units = "Seconds"))
	float Lifetime = 0.55f;

	UPROPERTY(EditAnywhere, Category = "BrakeDust|Override", meta = (EditCondition = "bOverrideParameters"))
	FLinearColor DustColor = FLinearColor(0.55f, 0.40f, 0.26f, 1.0f);

	/** 同じMeshから連続して出す最短間隔（秒）です。Montageの再開などで同じ接地が重なっても無制限に生成しません。 */
	UPROPERTY(EditAnywhere, Category = "BrakeDust", meta = (ClampMin = "0.0", Units = "Seconds"))
	float MinInterval = 0.02f;
};
