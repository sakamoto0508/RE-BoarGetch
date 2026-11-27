#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoarStagePreviewActor.generated.h"

class UStaticMesh;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/** ステージのミニチュアを構成する1部品の描画設定を保持する。 */
USTRUCT(BlueprintType)
struct FBoarPreviewPart
{
	GENERATED_BODY()

	/** このプレビュー部品に表示するメッシュ。 */
	UPROPERTY(EditAnywhere)
	TObjectPtr<UStaticMesh> Mesh;

	/** メッシュ既定の外観を差し替える場合のマテリアル。 */
	UPROPERTY(EditAnywhere)
	TObjectPtr<UMaterialInterface> Material;

	/** プレビューActorを基準とした部品の相対Transform。 */
	UPROPERTY(EditAnywhere)
	FTransform Transform;
};

/** ステージ表示用のミニチュア。Gameplayレベルをロードせず、定義されたMeshだけで構成します。 */
UCLASS(Blueprintable)
class REBOARGETCH_API ABoarStagePreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ABoarStagePreviewActor();

	/** ミニチュアを構成する部品設定の一覧。 */
	UPROPERTY(EditDefaultsOnly, Category="Preview")
	TArray<FBoarPreviewPart> Parts;

	/** ミニチュア撮影に使う平行投影の幅（cm）。 */
	UPROPERTY(EditDefaultsOnly, Category="Preview")
	float OrthoWidth = 1250.f;

	/** 静止画を一度撮影し、以降は同じRenderTargetを返します。 */
	UTextureRenderTarget2D* CreatePreview();
	virtual void Tick(float DeltaSeconds) override;
	/** シェーダーやテクスチャの準備待ちとして、生成後に撮り直しを続ける最短時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category="Preview")
	float RecaptureDuration = 1.5f;

private:
	/** マテリアルが準備されるまでの再撮影に使う残り時間（秒）。 */
	float RecaptureRemaining = 0.f;

	/** ミニチュアをカード用画像へ撮影するScene Capture。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	/** ミニチュアの撮影結果を書き込むRender Target。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;
};
