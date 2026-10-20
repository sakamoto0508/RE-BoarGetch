#include "RetroRenderEditorLibrary.h"
#include "RetroRenderEditorTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "UObject/Package.h"
#include "ShaderCompiler.h"
#include "SRetroRenderPanel.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Framework/Application/SlateApplication.h"

bool URetroRenderEditorLibrary::RenderRetroPanelToPng(const FString& Path, int32 Tab, int32 SelectPreset, const TArray<UTexture2D*>& PreviewTextures, int32 Width, int32 Height)
{
	TSharedRef<SRetroRenderPanel> Panel = SNew(SRetroRenderPanel);
	Panel->SetActiveTab(Tab);
	if (SelectPreset >= 0 && SelectPreset <= 4) Panel->TestSelectPreset(ERetroRenderPreset(SelectPreset));
	if (!PreviewTextures.IsEmpty()) Panel->TestPreviewTextures(PreviewTextures);
	FWidgetRenderer Renderer(true);
	const FVector2D Size(Width, Height);
	// Two passes: the first lays out the details views, the second draws the settled layout.
	UTextureRenderTarget2D* Target = Renderer.DrawWidget(Panel, Size);
	for (int32 Pass = 0; Pass < 4; ++Pass) { FSlateApplication::Get().Tick(); Renderer.DrawWidget(Target, Panel, Size, 0.016f); }
	FlushRenderingCommands();
	FImage Image;
	if (!Target || !FImageUtils::GetRenderTargetImage(Target, Image)) return false;
	return FImageUtils::SaveImageByExtension(*Path, Image);
}

int32 URetroRenderEditorLibrary::GetRemainingShaderJobs()
{
	return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
}

void URetroRenderEditorLibrary::FinishShaderCompilation()
{
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
}

int32 URetroRenderEditorLibrary::RemoveRetroExpressions(UObject* Owner, const TArray<UMaterialExpression*>& Expressions)
{
	UMaterial* Material = Cast<UMaterial>(Owner);
	UMaterialFunction* Function = Cast<UMaterialFunction>(Owner);
	if (!Material && !Function) return 0;
	FMaterialExpressionCollection& Collection = Material ? Material->GetExpressionCollection() : Function->GetExpressionCollection();
	TArray<UMaterialExpression*> All;
	for (UMaterialExpression* E : Collection.Expressions) All.Add(E);
	TArray<UMaterialExpression*> Remove = Expressions.IsEmpty() ? All : Expressions;
	Owner->Modify();
	int32 Count = 0;
	for (UMaterialExpression* X : Remove)
	{
		if (!X || X->GetOuter() != Owner) continue;
		for (UMaterialExpression* E : All)
		{
			if (!E) continue;
			for (FExpressionInputIterator It{E}; It; ++It) if (It->Expression == X) It->Expression = nullptr;
		}
		if (Material)
		{
			for (int32 P = 0; P < MP_MAX; ++P)
			{
				if (FExpressionInput* In = Material->GetExpressionInputForProperty(EMaterialProperty(P))) if (In->Expression == X) In->Expression = nullptr;
			}
		}
		Collection.RemoveExpression(X);
		if (X->IsRooted()) UE_LOG(LogTemp, Warning, TEXT("[Retro] %s is rooted; detached without garbage marking"), *X->GetName());
		X->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		++Count;
	}
	Owner->MarkPackageDirty();
	return Count;
}

FString URetroRenderEditorLibrary::RetroTextures(const TArray<UTexture2D*>& Textures, int32 MaxSize, bool bPointFiltering, bool bDisableMipmaps,
	bool bSkipNormalMaps, bool bApply, bool bSave)
{
	URetroTextureOptions* Options = NewObject<URetroTextureOptions>();
	Options->MaxTextureSize = MaxSize <= 64 ? ERetroTextureSize::S64 : MaxSize <= 128 ? ERetroTextureSize::S128 : MaxSize <= 256 ? ERetroTextureSize::S256 : ERetroTextureSize::S512;
	Options->bPointFiltering = bPointFiltering;
	Options->bDisableMipmaps = bDisableMipmaps;
	Options->bSkipNormalMaps = bSkipNormalMaps;
	Options->bSaveAfterApply = bSave;
	const TArray<RetroTextureUtility::FPlanItem> Plan = RetroTextureUtility::Plan(Textures, *Options, false);
	FString Report = RetroTextureUtility::Describe(Plan, false);
	if (bApply) Report = FString::Printf(TEXT("Applied to %d textures.\n"), RetroTextureUtility::Apply(Plan, *Options)) + Report;
	return Report;
}

FString URetroRenderEditorLibrary::RevertRetroTextures(const TArray<UTexture2D*>& Textures, bool bApply, bool bSave)
{
	const TArray<RetroTextureUtility::FPlanItem> Plan = RetroTextureUtility::Plan(Textures, *GetDefault<URetroTextureOptions>(), true);
	FString Report = RetroTextureUtility::Describe(Plan, true);
	if (bApply) Report = FString::Printf(TEXT("Reverted %d textures.\n"), RetroTextureUtility::Revert(Plan, bSave)) + Report;
	return Report;
}

TArray<UStaticMesh*> URetroRenderEditorLibrary::BakeRetroMeshes(const TArray<UStaticMesh*>& Meshes, FString& Report, float PercentTriangles,
	int32 TargetTriangles, int32 MinimumTriangles, float HardAngleThreshold, bool bRecalculateNormals, const FString& OutputFolder)
{
	URetroDecimateOptions* Options = NewObject<URetroDecimateOptions>();
	Options->Mode = TargetTriangles > 0 ? ERetroDecimateMode::TargetTriangles : ERetroDecimateMode::Percent;
	Options->PercentTriangles = PercentTriangles;
	Options->TargetTriangleCount = FMath::Max(TargetTriangles, 4);
	Options->MinimumTriangles = MinimumTriangles;
	Options->HardAngleThreshold = HardAngleThreshold;
	Options->bRecalculateNormals = bRecalculateNormals;
	Options->OutputFolder.Path = OutputFolder;
	TArray<RetroMeshBaker::FResult> Results;
	TArray<UStaticMesh*> Baked;
	for (UStaticMesh* Mesh : Meshes)
	{
		Results.Add(RetroMeshBaker::Bake(Mesh, *Options));
		Baked.Add(Results.Last().Baked.Get());
	}
	Report = RetroMeshBaker::Describe(Results);
	return Baked;
}
