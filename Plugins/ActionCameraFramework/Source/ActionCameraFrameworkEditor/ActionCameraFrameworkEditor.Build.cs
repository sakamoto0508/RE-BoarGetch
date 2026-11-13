using UnrealBuildTool;

public class ActionCameraFrameworkEditor : ModuleRules
{
	public ActionCameraFrameworkEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] { "ActionCameraFramework", "UnrealEd", "AssetRegistry", "DataValidation" });
	}
}
