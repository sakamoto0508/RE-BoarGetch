#pragma once
#include "CoreMinimal.h"

class UWorld;

namespace RetroLevelMaterials
{
	// Project (/Game) surface materials and FBX-imported instances used in World that still lack the RetroRender
	// geometry patch. Skies and Material Attributes graphs are not counted (they are never patched).
	TArray<FString> FindUnpatched(UWorld* World);
	// Runs Scripts/PatchRetroMaterials.py -RetroLevelScan in this editor and returns a readable summary.
	FString RunLevelScan(bool bDryRun);
}
