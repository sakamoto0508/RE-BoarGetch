#include "UI/BoarStagePreviewActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ShaderCompiler.h"

namespace
{
	/** ミニチュアを斜め上から見下ろす撮影位置と、注視点（Actor基準、cm）。 */
	const FVector PreviewCaptureLocation(-1100, -1100, 1100);
	const FVector PreviewLookAtLocation(0, 0, 80);

	/** 撮影するRender Targetの解像度（px）。 */
	constexpr int32 PreviewTextureWidth = 1024;
	constexpr int32 PreviewTextureHeight = 768;
}

ABoarStagePreviewActor::ABoarStagePreviewActor()
{
	// 生成直後はシェーダー/テクスチャが未準備の場合があるため、短時間だけTickで撮り直します。
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(GetRootComponent());
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->SetRelativeLocation(PreviewCaptureLocation);
	Capture->SetRelativeRotation((PreviewLookAtLocation - Capture->GetRelativeLocation()).Rotation());
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetBloom(false);
}

// 定義されたMeshだけを撮影対象に登録します。衝突・Navigationへの影響を無効にした表示専用の構成です。
UTextureRenderTarget2D* ABoarStagePreviewActor::CreatePreview()
{
	if (Target)
		return Target;
	if (Parts.IsEmpty())
		return nullptr;

	int32 ValidParts = 0;

	for (const auto& Part : Parts)
	{
		if (!Part.Mesh)
			continue;

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

		if (Part.Material)
			Mesh->SetMaterial(0, Part.Material);

		Mesh->RegisterComponent();

		// 画面外に置くため通常のストリーミングでは低解像度のままになりやすく、先読みを要求します。
		Mesh->PrestreamTextures(RecaptureDuration + 1.f, false);
		Capture->ShowOnlyComponent(Mesh);
		++ValidParts;
	}

	if (!ValidParts)
		return nullptr;

	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->ClearColor = FLinearColor(0, 0, 0, 1); // Captureのアルファには不透明度の反転値が入ります。
	Target->InitCustomFormat(PreviewTextureWidth, PreviewTextureHeight, PF_FloatRGBA, false);
	Target->UpdateResourceImmediate(true);
	Capture->TextureTarget = Target;
	Capture->OrthoWidth = OrthoWidth;
	Capture->CaptureScene();

	// 生成と同じフレームの撮影はデフォルトマテリアルで写ることがあるため、準備完了まで撮り直します。
	RecaptureRemaining = RecaptureDuration;
	SetActorTickEnabled(true);
	return Target;
}

void ABoarStagePreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Target)
	{
		SetActorTickEnabled(false);
		return;
	}

	Capture->CaptureScene();
	RecaptureRemaining -= DeltaSeconds;
	bool bStillCompiling = false;
#if WITH_EDITOR
	// PIEではシェーダーが非同期コンパイル中だとデフォルトマテリアルで描画されるため、完了まで待ちます。
	bStillCompiling = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
#endif
	// 静止モデルなので、準備が整った後は撮影を止めます。
	if (RecaptureRemaining <= 0.f && !bStillCompiling)
		SetActorTickEnabled(false);
}
