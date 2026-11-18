#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ACFCameraTypes.h"
#include "ACFCameraPreset.generated.h"

class UACFCameraNode;

/**
 * Shareable, designer-authored description of one camera ("virtual camera" in Cinemachine
 * terms): the rig node stack plus default blends.
 *
 * Contains configuration only. Runtime state lives in FACFCameraRigInstance, created per
 * request, so the same preset can safely drive several local players at once.
 */
UCLASS(BlueprintType, meta = (ToolTip = "通常TPSやロックオンなど、1つのカメラの動きと切り替えを設定します。Rig で構成ノードを編集します。"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** カメラを構成するノードです。位置、向き、構図、ブレンド後の補正の順に評価し、同じ段階では配列順が基本です。通常TPSでは Orbital Follow で位置を作り、Collision で壁めり込みを防ぎます。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Rig (構成ノード)")
	TArray<TObjectPtr<UACFCameraNode>> Nodes;

	/** このプリセットへ切り替わるときの既定ブレンドです。カメラリクエストごとに上書きできます。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend (切り替え)")
	FACFCameraBlendSettings DefaultBlendIn;

	/** このプリセットの使用を終え、UEのカメラへ制御を戻すときの既定ブレンドです。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend (切り替え)")
	FACFCameraBlendSettings DefaultBlendOut;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
