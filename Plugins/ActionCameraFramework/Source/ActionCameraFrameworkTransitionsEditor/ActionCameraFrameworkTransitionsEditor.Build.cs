using UnrealBuildTool;

// Phase 7D: the editor side of the transitions (README 33 章「Editor Preview」): the "ACF Transition Preview" tab and the
// editor-only tests (preview panel, PIE level travel). The runtime module never depends on it.
public class ActionCameraFrameworkTransitionsEditor : ModuleRules
{
	public ActionCameraFrameworkTransitionsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ActionCameraFrameworkTransitions", "UnrealEd", "Slate", "SlateCore", "InputCore", "PropertyEditor",
			"WorkspaceMenuStructure", "AssetTools", "ContentBrowser", "AssetRegistry", "ToolMenus",
		});
	}
}
