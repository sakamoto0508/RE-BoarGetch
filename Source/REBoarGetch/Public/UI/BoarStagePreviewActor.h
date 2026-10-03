#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoarStagePreviewActor.generated.h"

class UStaticMesh;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

USTRUCT(BlueprintType)
struct FBoarPreviewPart
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere) FTransform Transform;
};

/** ステージ表示用のミニチュア。Gameplayレベルをロードせず、定義されたMeshだけで構成します。 */
UCLASS(Blueprintable)
class REBOARGETCH_API ABoarStagePreviewActor : public AActor
{
	GENERATED_BODY()
public:
	ABoarStagePreviewActor();
	UPROPERTY(EditDefaultsOnly, Category="Preview") TArray<FBoarPreviewPart> Parts;
	UPROPERTY(EditDefaultsOnly, Category="Preview") float OrthoWidth = 1250.f;
	/** 静止画を一度撮影し、以降は同じRenderTargetを返します。 */
	UTextureRenderTarget2D* CreatePreview();
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
};
