using UnrealBuildTool;

// Level Sequencer との連携（Phase 5A）。Sequencer への依存はこのモジュールだけに閉じ込める:
// 基本の Runtime モジュール（ActionCameraFramework）は LevelSequence / MovieScene を参照しない。
public class ActionCameraFrameworkSequencer : ModuleRules
{
	public ActionCameraFrameworkSequencer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ActionCameraFramework", "LevelSequence", "MovieScene" });
		PrivateDependencyModuleNames.AddRange(new[] { "DeveloperSettings", "MovieSceneTracks" });
	}
}
