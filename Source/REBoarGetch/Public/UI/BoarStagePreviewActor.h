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

/** Presentation-only miniature. Never loads a gameplay level or spawns gameplay actors. */
UCLASS(Blueprintable)
class REBOARGETCH_API ABoarStagePreviewActor : public AActor
{
	GENERATED_BODY()
public:
	ABoarStagePreviewActor();
	UPROPERTY(EditDefaultsOnly, Category="Preview") TArray<FBoarPreviewPart> Parts;
	UPROPERTY(EditDefaultsOnly, Category="Preview") float OrthoWidth = 1250.f;
	UTextureRenderTarget2D* CreatePreview();
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
};
