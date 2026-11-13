using UnrealBuildTool;

// Phase 6B / B-10: the automation tests of the Core and Sequencer modules and their test-only classes (test camera
// shake, sequencer listener / probe modifier / test PlayerCameraManagers, test environment).
// Type DeveloperTool in the .uplugin: built for the editor and for Debug / Development game targets (tests also run with
// -game), never for Test / Shipping. Nothing in ActionCameraFramework / ActionCameraFrameworkSequencer depends on this
// module; it only uses their exported API.
public class ActionCameraFrameworkTests : ModuleRules
{
	public ActionCameraFrameworkTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "DeveloperSettings",
			"ActionCameraFramework", "ActionCameraFrameworkSequencer",
			"LevelSequence", "MovieScene", "MovieSceneTracks",
		});
	}
}
