using UnrealBuildTool;

public class ActionCameraFramework : ModuleRules
{
	public ActionCameraFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Runtime must never depend on editor modules (see README.md).
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] { "DeveloperSettings" });
	}
}
