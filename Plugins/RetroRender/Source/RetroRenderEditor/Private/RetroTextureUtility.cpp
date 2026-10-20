#include "RetroRenderEditorTypes.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

// Original settings live in the texture package metadata, so Revert works across sessions.
// Source art is never modified: MaxTextureSize / Filter / MipGen only change the cooked/derived data.
namespace RetroTextureUtility
{
	static const TCHAR* OriginalKey = TEXT("RetroRender.OriginalTextureSettings");

	static FString Encode(const UTexture2D* T)
	{
		return FString::Printf(TEXT("%d;%d;%d"), int32(T->Filter), T->MaxTextureSize, int32(T->MipGenSettings));
	}

	static bool HasOriginal(UTexture2D* T)
	{
		return T->GetPackage()->GetMetaData().HasValue(T, OriginalKey);
	}

	static FString Current(const UTexture2D* T)
	{
		static const TCHAR* Filters[] = {TEXT("Nearest"), TEXT("Bilinear"), TEXT("Trilinear"), TEXT("Default")};
		return FString::Printf(TEXT("%dx%d max=%d filter=%s mips=%s"), int32(T->Source.GetSizeX()), int32(T->Source.GetSizeY()),
			T->MaxTextureSize, Filters[FMath::Clamp(int32(T->Filter), 0, 3)], T->MipGenSettings == TMGS_NoMipmaps ? TEXT("off") : TEXT("on"));
	}

	TArray<FPlanItem> Plan(const TArray<UTexture2D*>& Textures, const URetroTextureOptions& Options, bool bRevert)
	{
		TArray<FPlanItem> Items;
		for (UTexture2D* T : Textures)
		{
			FPlanItem& Item = Items.AddDefaulted_GetRef();
			Item.Texture = T;
			if (bRevert)
			{
				Item.bSkip = !HasOriginal(T);
				Item.Note = Item.bSkip ? TEXT("no stored original") : TEXT("restore ") + T->GetPackage()->GetMetaData().GetValue(T, OriginalKey);
				continue;
			}
			if (Options.bSkipNormalMaps && T->IsNormalMap()) { Item.bSkip = true; Item.Note = TEXT("skip: normal map"); continue; }
			if (Options.bSkipUITextures && T->LODGroup == TEXTUREGROUP_UI) { Item.bSkip = true; Item.Note = TEXT("skip: UI texture group"); continue; }
			if (Options.bSkipNonColorTextures && !T->SRGB) { Item.bSkip = true; Item.Note = TEXT("skip: non-colour (linear) data"); continue; }
			const int32 Longest = FMath::Max(int32(T->Source.GetSizeX()), int32(T->Source.GetSizeY()));
			TArray<FString> Changes;
			if (Longest > Options.GetMaxSize() && T->MaxTextureSize != Options.GetMaxSize()) Changes.Add(FString::Printf(TEXT("max %d"), Options.GetMaxSize()));
			if (Options.bPointFiltering && T->Filter != TF_Nearest) Changes.Add(TEXT("nearest"));
			if (Options.bDisableMipmaps && T->MipGenSettings != TMGS_NoMipmaps) Changes.Add(TEXT("no mips"));
			Item.bSkip = Changes.IsEmpty();
			Item.Note = Item.bSkip ? TEXT("already retro") : Current(T) + TEXT("  ->  ") + FString::Join(Changes, TEXT(", "));
		}
		return Items;
	}

	FString Describe(const TArray<FPlanItem>& Items, bool bRevert)
	{
		FString Out;
		int32 Count = 0;
		for (const FPlanItem& Item : Items)
		{
			if (!Item.Texture.IsValid()) continue;
			Count += Item.bSkip ? 0 : 1;
			Out += FString::Printf(TEXT("%s %s : %s\n"), Item.bSkip ? TEXT("  -") : TEXT("  +"), *Item.Texture->GetName(), *Item.Note);
		}
		return FString::Printf(TEXT("%s: %d of %d textures will change\n"), bRevert ? TEXT("Revert") : TEXT("Apply"), Count, Items.Num()) + Out;
	}

	static void Save(const TArray<UPackage*>& Packages)
	{
		if (!Packages.IsEmpty()) UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
	}

	int32 Apply(const TArray<FPlanItem>& Items, const URetroTextureOptions& Options)
	{
		TArray<UPackage*> Packages;
		for (const FPlanItem& Item : Items)
		{
			UTexture2D* T = Item.Texture.Get();
			if (!T || Item.bSkip) continue;
			T->Modify();
			if (!HasOriginal(T)) T->GetPackage()->GetMetaData().SetValue(T, OriginalKey, *Encode(T));
			const int32 Longest = FMath::Max(int32(T->Source.GetSizeX()), int32(T->Source.GetSizeY()));
			if (Longest > Options.GetMaxSize()) T->MaxTextureSize = Options.GetMaxSize();
			if (Options.bPointFiltering) T->Filter = TF_Nearest;
			if (Options.bDisableMipmaps) T->MipGenSettings = TMGS_NoMipmaps;
			T->PostEditChange();
			T->MarkPackageDirty();
			Packages.Add(T->GetPackage());
		}
		if (Options.bSaveAfterApply) Save(Packages);
		return Packages.Num();
	}

	int32 Revert(const TArray<FPlanItem>& Items, bool bSave)
	{
		TArray<UPackage*> Packages;
		for (const FPlanItem& Item : Items)
		{
			UTexture2D* T = Item.Texture.Get();
			if (!T || Item.bSkip) continue;
			TArray<FString> Parts;
			T->GetPackage()->GetMetaData().GetValue(T, OriginalKey).ParseIntoArray(Parts, TEXT(";"));
			if (Parts.Num() != 3) continue;
			T->Modify();
			T->Filter = TextureFilter(FCString::Atoi(*Parts[0]));
			T->MaxTextureSize = FCString::Atoi(*Parts[1]);
			T->MipGenSettings = TextureMipGenSettings(FCString::Atoi(*Parts[2]));
			T->GetPackage()->GetMetaData().RemoveValue(T, OriginalKey);
			T->PostEditChange();
			T->MarkPackageDirty();
			Packages.Add(T->GetPackage());
		}
		if (bSave) Save(Packages);
		return Packages.Num();
	}
}
