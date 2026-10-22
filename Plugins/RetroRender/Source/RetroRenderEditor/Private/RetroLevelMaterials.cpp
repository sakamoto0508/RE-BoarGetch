#include "RetroLevelMaterials.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "IPythonScriptPlugin.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RetroLevelMaterials
{
	static const TCHAR* FbxPhong = TEXT("/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial");

	static bool IsPatched(UMaterial* Material)
	{
		const FExpressionInput* Input = Material->GetExpressionInputForProperty(MP_WorldPositionOffset);
		return Input && Input->Expression && Input->Expression->Desc.StartsWith(TEXT("RetroRender"));
	}

	TArray<FString> FindUnpatched(UWorld* World)
	{
		TSet<FString> Result;
		if (!World) return {};
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UPrimitiveComponent*> Components;
			It->GetComponents(Components);
			for (UPrimitiveComponent* Component : Components)
			{
				for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
				{
					UMaterialInterface* Interface = Component->GetMaterial(Index);
					UMaterial* Base = Interface ? Interface->GetMaterial() : nullptr;
					if (!Base) continue;
					const FString BasePath = Base->GetOutermost()->GetName();
					if (BasePath == FbxPhong)
					{
						// Imported instance not yet reparented to M_RetroFBXPhong.
						if (Interface->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))) Result.Add(Interface->GetOutermost()->GetName());
						continue;
					}
					if (!BasePath.StartsWith(TEXT("/Game/")) || Base->MaterialDomain != MD_Surface || Base->bUseMaterialAttributes) continue;
					if (Base->GetName().Contains(TEXT("Sky"))) continue;
					if (!IsPatched(Base)) Result.Add(BasePath);
				}
			}
		}
		TArray<FString> Sorted = Result.Array();
		Sorted.Sort();
		return Sorted;
	}

	FString RunLevelScan(bool bDryRun)
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!Python || !Python->IsPythonAvailable()) return TEXT("Python Editor Script Plugin is not available.");
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RetroRender"));
		if (!Plugin) return TEXT("RetroRender plugin not found.");
		const FString Script = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Scripts/PatchRetroMaterials.py"));
		const FString ResultFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RetroRender/LevelScan.json"));
		IFileManager::Get().Delete(*ResultFile);
		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Command.Command = FString::Printf(TEXT("\"%s\" -RetroLevelScan%s"), *Script, bDryRun ? TEXT(" -RetroDryRun") : TEXT(""));
		Python->ExecPythonCommandEx(Command);

		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *ResultFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
		{
			return TEXT("Level scan failed; see the Output Log ([RetroPatch]).");
		}
		TMap<FString, int32> Counts;
		FString Lines;
		const TSharedPtr<FJsonObject>* Materials = nullptr;
		if (Root->TryGetObjectField(TEXT("materials"), Materials))
		{
			for (const auto& Pair : (*Materials)->Values)
			{
				const TSharedPtr<FJsonObject> Entry = Pair.Value->AsObject();
				const FString Decision(Entry->GetStringField(TEXT("decision")));
				Counts.FindOrAdd(Decision)++;
				if (Decision != TEXT("ok"))
				{
					const FString Path(Pair.Key);
					const FString Detail(Entry->GetStringField(TEXT("detail")));
					Lines += FString::Printf(TEXT("  [%s] %s  (%s, %d uses)\n"), *Decision, *FPaths::GetBaseFilename(Path),
						*Detail, int32(Entry->GetNumberField(TEXT("uses"))));
				}
			}
		}
		const FString Level(Root->GetStringField(TEXT("level")));
		FString Summary = FString::Printf(TEXT("%s %s: "), bDryRun ? TEXT("Scan") : TEXT("Patch"), *Level);
		for (const TPair<FString, int32>& Count : Counts) Summary += FString::Printf(TEXT("%s=%d  "), *Count.Key, Count.Value);
		return Summary + TEXT("\n") + Lines;
	}
}
