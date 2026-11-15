using UnrealBuildTool;

// Phase 7D: screen / level transitions (README 33 章). A runtime module of its own that depends on engine modules only:
// it does not use the camera Core (ActionCameraFramework), so a game can use transitions without camera requests, and
// the Core does not know this module. MoviePlayer is optional at run time (the loading screen during a blocking
// OpenLevel in a standalone / packaged game, UACFTransitionSettings::bUseMoviePlayerDuringLoad).
public class ActionCameraFrameworkTransitions : ModuleRules
{
	public ActionCameraFrameworkTransitions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "SlateCore", "Slate" });
		PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "RHI", "MoviePlayer", "ApplicationCore" });
	}
}
