#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ACFCameraTypes.h"
#include "CaptureCameraPresentationData.generated.h"

class UACFCameraEffect;
class UACFCameraNode_Collision;
class UACFCameraNode_SpiralOrbit;
class UACFCameraSequence;

/**
 * 捕獲成功時のカメラ演出（AKI-165）を1つのDataAssetにまとめたもの：Boarを中心にした螺旋上昇の回転（Spiral Orbit）、
 * 壁際の処理（Collision）、時間とBlend、捕獲の揺れ（Camera Effect Layer）。
 * UCapturePresentationComponentが実行時にこの内容からACFのPreset・Effect Preset・Camera Sequenceを組み立てて再生する
 * （ACFが唯一のカメラ出力。Gameplayの時刻には影響しない）。
 */
UCLASS(BlueprintType)
class REBOARGETCH_API UCaptureCameraPresentationData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UCaptureCameraPresentationData();

	/** 回転：Boar（注視点）を中心に、カメラ位置が円を描いて公転しながら上昇する。Duration=一周の秒数（短いほど速い）、Rotation Degrees=回る角度、Height/Radius=高さと距離、Angle Ease Exponent=加減速。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "1. Orbit (回転)")
	TObjectPtr<UACFCameraNode_SpiralOrbit> Orbit;

	/** 回転が終わってから通常カメラへ戻り始めるまで、最後の位置で止まる秒数。演出の長さ = Orbit.Duration + これ。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "1. Orbit (回転)", meta = (ClampMin = "0.0", ClampMax = "2.0", Units = s))
	float HoldAfterOrbit = 0.1f;

	/** 通常カメラから回転カメラへ入る切り替え。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "1. Orbit (回転)")
	FACFCameraBlendSettings BlendIn;

	/** 回転の後、通常のTPSカメラへ戻る切り替え（Duration=戻る速さ）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "1. Orbit (回転)")
	FACFCameraBlendSettings ReturnBlend;

	/** 揺れ：回転の開始と同時に1回再生する（Camera Punch=一瞬の寄り、Camera Impulse=押し返す減衰振動、Camera Shake (UE) など）。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "2. Shake (揺れ)")
	TArray<TObjectPtr<UACFCameraEffect>> ShakeLayers;

	/** 揺れ全体の強さ（1=そのまま、0.5=半分、0=揺らさない）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "2. Shake (揺れ)", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float ShakeScale = 1.f;

	/** 壁際：カメラが壁や地面にめり込まないよう、注視点の方へ寄せる（通常TPSと同じ設定を推奨）。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "3. Collision (壁際)")
	TObjectPtr<UACFCameraNode_Collision> Collision;

	/** ACF Requestの優先度。通常カメラ（0）より上、Finisher（1000）より下。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "4. Advanced")
	int32 Priority = 900;

	/** この内容からACFのCamera Sequence（Preset・Effect Presetを含む一時オブジェクト）を作る。Orbitが無ければnull。 */
	UACFCameraSequence* BuildSequence(UObject* Outer) const;
};
