#include "UI/BoarStagePreviewActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"

ABoarStagePreviewActor::ABoarStagePreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(GetRootComponent());
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->SetRelativeLocation(FVector(-1100, -1100, 1100));
	Capture->SetRelativeRotation((FVector(0,0,80) - Capture->GetRelativeLocation()).Rotation());
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetBloom(false);
}

// 定義されたMeshだけを撮影対象に登録します。衝突・Navigationへの影響を無効にした表示専用の構成です。
UTextureRenderTarget2D* ABoarStagePreviewActor::CreatePreview()
{
	if (Target) return Target;
	if (Parts.IsEmpty()) return nullptr;
	int32 ValidParts = 0;
	for (const auto& Part : Parts)
	{
		if (!Part.Mesh) continue;
		auto* Mesh = NewObject<UStaticMeshComponent>(this);
		AddInstanceComponent(Mesh);
		Mesh->SetupAttachment(GetRootComponent());
		Mesh->SetStaticMesh(Part.Mesh);
		Mesh->bUseDefaultCollision = false;
		Mesh->SetCollisionProfileName(TEXT("NoCollision"));
		Mesh->SetRelativeTransform(Part.Transform);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		Mesh->SetVisibleInSceneCaptureOnly(true);
		if (Part.Material) Mesh->SetMaterial(0, Part.Material);
		Mesh->RegisterComponent();
		Capture->ShowOnlyComponent(Mesh);
		++ValidParts;
	}
	if (!ValidParts) return nullptr;
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->ClearColor = FLinearColor(0,0,0,1); // Captureのアルファには不透明度の反転値が入ります。
	Target->InitCustomFormat(1024, 768, PF_FloatRGBA, false);
	Target->UpdateResourceImmediate(true);
	Capture->TextureTarget = Target;
	Capture->OrthoWidth = OrthoWidth;
	Capture->CaptureScene(); // 静止モデルなのでプレビュー作成時だけ撮影します。
	return Target;
}
