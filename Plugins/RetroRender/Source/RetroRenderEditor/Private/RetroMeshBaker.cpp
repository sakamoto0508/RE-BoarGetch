#include "RetroRenderEditorTypes.h"
#include "AssetToolsModule.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "StaticMeshResources.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "RetroMeshBaker"

// Offline, editor-only: the reduction runs inside the static mesh build of a duplicated asset.
// The source mesh (and its source data) is never modified; the copy keeps sections, UVs, sockets
// and simple collision because it is a full duplicate.
namespace RetroMeshBaker
{
	static const FString TagPrefix = TEXT("RetroOriginalMesh=");
	// Package metadata written on every bake: identifies assets this tool owns and may re-bake.
	static const TCHAR* BakedFromKey = TEXT("RetroRender.BakedFrom");

	static FString BakedFrom(UStaticMesh* Mesh)
	{
		FMetaData& Meta = Mesh->GetPackage()->GetMetaData();
		return Meta.HasValue(Mesh, BakedFromKey) ? Meta.GetValue(Mesh, BakedFromKey) : FString();
	}

	static FString OutputPackagePath(UStaticMesh* Source, const URetroDecimateOptions& Options)
	{
		FString Folder = Options.OutputFolder.Path;
		if (Folder.IsEmpty()) return FPackageName::GetLongPackagePath(Source->GetOutermost()->GetName());
		if (!Folder.StartsWith(TEXT("/"))) Folder = TEXT("/Game/") + Folder;
		return Folder;
	}

	FResult Bake(UStaticMesh* Source, const URetroDecimateOptions& Options)
	{
		FResult R;
		R.Source = Source;
		if (!Source) { R.Note = TEXT("invalid"); return R; }
		if (Source->GetName().EndsWith(Options.Suffix)) { R.Baked = Source; R.Note = TEXT("already a retro bake"); return R; }
		R.SourceTriangles = Source->GetNumTriangles(0);
		const FString Path = OutputPackagePath(Source, Options);
		const FString Name = Source->GetName() + Options.Suffix;
		const FString ObjectPath = Path / Name + TEXT(".") + Name;
		UStaticMesh* Baked = LoadObject<UStaticMesh>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Baked)
		{
			const FString Origin = BakedFrom(Baked);
			if (Origin != Source->GetPathName())
			{
				R.Note = Origin.IsEmpty() ? TEXT("SKIPPED: an asset with this name exists and was not created by the baker")
					: TEXT("SKIPPED: existing bake belongs to ") + Origin;
				return R;
			}
			if (!Options.bOverwriteExisting)
			{
				R.Baked = Baked;
				R.BakedTriangles = Baked->GetNumTriangles(0);
				R.Note = TEXT("kept existing bake (enable Overwrite Existing to re-bake)");
				return R;
			}
		}
		if (!Baked)
		{
			IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			Baked = Cast<UStaticMesh>(Tools.DuplicateAsset(Name, Path, Source));
		}
		if (!Baked) { R.Note = TEXT("duplicate failed"); return R; }

		Baked->Modify();
		Baked->SetNumSourceModels(1); // a PS1-era mesh has one LOD
		FStaticMeshSourceModel& Model = Baked->GetSourceModel(0);
		FMeshReductionSettings& Reduction = Model.ReductionSettings;
		Reduction.TerminationCriterion = EStaticMeshReductionTerimationCriterion::Triangles;
		float Percent = Options.Mode == ERetroDecimateMode::Percent ? Options.PercentTriangles
			: float(Options.TargetTriangleCount) / float(FMath::Max(R.SourceTriangles, 1));
		if (R.SourceTriangles <= Options.MinimumTriangles) Percent = 1.f;
		Reduction.PercentTriangles = FMath::Clamp(Percent, 0.01f, 1.f);
		Reduction.MaxNumOfTriangles = Options.Mode == ERetroDecimateMode::TargetTriangles ? uint32(FMath::Max(Options.TargetTriangleCount, 4)) : MAX_uint32;
		Reduction.bRecalculateNormals = Options.bRecalculateNormals;
		Reduction.HardAngleThreshold = Options.HardAngleThreshold;
		Reduction.bKeepSymmetry = Options.bKeepSymmetry;
		Reduction.SilhouetteImportance = EMeshFeatureImportance::Highest;
		Reduction.TextureImportance = EMeshFeatureImportance::High;
		if (Options.bDisableNanite)
		{
			FMeshNaniteSettings Nanite = Baked->GetNaniteSettings();
			Nanite.bEnabled = false;
			Baked->SetNaniteSettings(Nanite);
		}
		if (Options.bKeepOriginalComplexCollision) Baked->ComplexCollisionMesh = Source;
		Baked->GetPackage()->GetMetaData().SetValue(Baked, BakedFromKey, *Source->GetPathName());
		Baked->Build(true);
		Baked->PostEditChange();
		Baked->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({Baked->GetPackage()}, true);
		R.Baked = Baked;
		R.BakedTriangles = Baked->GetNumTriangles(0);
		R.Note = Percent >= 1.f ? TEXT("below minimum, copied without reduction") : FString::Printf(TEXT("%.0f%% target"), Reduction.PercentTriangles * 100.f);
		if (R.BakedTriangles >= R.SourceTriangles && Percent < 1.f)
		{
			// Normal recalculation can split vertices; if the reducer found nothing to remove, say so instead of hiding it.
			R.Note += TEXT(" - WARNING: not reduced (check hard-angle / mesh topology)");
		}
		return R;
	}

	FString Describe(const TArray<FResult>& Results)
	{
		int64 Before = 0, After = 0;
		FString Out;
		for (const FResult& R : Results)
		{
			Before += R.SourceTriangles; After += R.BakedTriangles;
			Out += FString::Printf(TEXT("  %s : %d -> %d tris (%s)%s\n"), R.Source.IsValid() ? *R.Source->GetName() : TEXT("?"),
				R.SourceTriangles, R.BakedTriangles, *R.Note, R.Baked.IsValid() ? *(TEXT("  => ") + R.Baked->GetPathName()) : TEXT(""));
		}
		return FString::Printf(TEXT("Baked %d meshes, %lld -> %lld triangles\n"), Results.Num(), Before, After) + Out;
	}

	static void ForSelectedMeshComponents(TFunctionRef<void(UStaticMeshComponent*)> Fn)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				TArray<UStaticMeshComponent*> Components;
				Actor->GetComponents(Components);
				for (UStaticMeshComponent* C : Components) Fn(C);
			}
		}
	}

	FString ReplaceSelected(const URetroDecimateOptions& Options)
	{
		const FScopedTransaction Transaction(LOCTEXT("Replace", "Replace with Retro Meshes"));
		TMap<UStaticMesh*, FResult> Cache;
		TArray<FResult> Baked;
		int32 Replaced = 0;
		ForSelectedMeshComponents([&](UStaticMeshComponent* C)
		{
			UStaticMesh* Mesh = C->GetStaticMesh();
			if (!Mesh || Mesh->GetName().EndsWith(Options.Suffix)) return;
			if (!Cache.Contains(Mesh)) { Cache.Add(Mesh, Bake(Mesh, Options)); Baked.Add(Cache[Mesh]); }
			const FResult& Result = Cache[Mesh];
			UStaticMesh* Retro = Result.Baked.Get();
			if (!Retro || Retro == Mesh) return;
			// A Nanite source can bake to more triangles than its fallback; never swap in a heavier mesh.
			if (Result.BakedTriangles >= Result.SourceTriangles && Result.SourceTriangles > Options.MinimumTriangles) return;
			C->Modify();
			C->ComponentTags.RemoveAll([](const FName& Tag) { return Tag.ToString().StartsWith(TagPrefix); });
			C->ComponentTags.Add(FName(TagPrefix + Mesh->GetPathName()));
			C->SetStaticMesh(Retro);
			++Replaced;
		});
		return FString::Printf(TEXT("Replaced %d components on selected actors\n"), Replaced) + Describe(Baked);
	}

	FString RestoreSelected()
	{
		const FScopedTransaction Transaction(LOCTEXT("Restore", "Restore Original Meshes"));
		int32 Restored = 0;
		ForSelectedMeshComponents([&](UStaticMeshComponent* C)
		{
			for (const FName& Tag : C->ComponentTags)
			{
				const FString Text = Tag.ToString();
				if (!Text.StartsWith(TagPrefix)) continue;
				if (UStaticMesh* Original = LoadObject<UStaticMesh>(nullptr, *Text.Mid(TagPrefix.Len())))
				{
					C->Modify();
					C->SetStaticMesh(Original);
					C->ComponentTags.Remove(Tag);
					++Restored;
				}
				break;
			}
		});
		return FString::Printf(TEXT("Restored %d components on selected actors\n"), Restored);
	}
}

#undef LOCTEXT_NAMESPACE
