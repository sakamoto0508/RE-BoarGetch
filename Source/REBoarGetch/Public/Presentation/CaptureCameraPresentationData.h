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

	/** 捕獲したイノシシの周りをカメラが回りながら上昇します。Duration は公転全体の秒数、Rotation Degrees は総回転角、Height と Radius は始点・終点の高さと距離です。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "1. Orbit (回転)")
	TObjectPtr<UACFCameraNode_SpiralOrbit> Orbit;

	/** 回転終了後、通常カメラへ戻り始めるまで最後の位置で待つ秒数です。回転と待機の合計は Orbit の Duration とこの値の和です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "1. Orbit (回転)", meta = (ClampMin = "0.0", ClampMax = "2.0", Units = s))
	float HoldAfterOrbit = 0.1f;

	/** 通常カメラから回転カメラへ入る切り替え。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "2. Blend (切り替え)")
	FACFCameraBlendSettings BlendIn;

	/** 通常の TPS カメラへ戻る切り替えです。Duration を長くするとゆっくり戻ります。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "2. Blend (切り替え)")
	FACFCameraBlendSettings ReturnBlend;

	/** 揺れ：回転の開始と同時に1回再生する（Camera Punch=一瞬の寄り、Camera Impulse=押し返す減衰振動、Camera Shake (UE) など）。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "3. Shake (揺れ)")
	TArray<TObjectPtr<UACFCameraEffect>> ShakeLayers;

	/** 揺れ全体の強さ（1=そのまま、0.5=半分、0=揺らさない）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "3. Shake (揺れ)", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float ShakeScale = 1.f;

	/** 壁際：カメラが壁や地面にめり込まないよう、注視点の方へ寄せる（通常TPSと同じ設定を推奨）。 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "4. Collision (壁際)")
	TObjectPtr<UACFCameraNode_Collision> Collision;

	/** ACF Requestの優先度。通常カメラ（0）より上、Finisher（1000）より下。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "5. Priority (優先度)")
	int32 Priority = 900;

	/** この内容からACFのCamera Sequence（Preset・Effect Presetを含む一時オブジェクト）を作る。Orbitが無ければnull。 */
	UACFCameraSequence* BuildSequence(UObject* Outer) const;
};
