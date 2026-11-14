using UnrealBuildTool;

// Phase 7C: level design actors (Camera Zone). They use the Core request API; the Core module (ActionCameraFramework)
// never references this module (README 32 章).
public class ActionCameraFrameworkLevel : ModuleRules
{
	public ActionCameraFrameworkLevel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ActionCameraFramework" });
	}
}
