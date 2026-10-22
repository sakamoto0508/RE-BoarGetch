using UnrealBuildTool;

public class RetroRenderEditor : ModuleRules
{
	public RetroRenderEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] {
			"RetroRender", "UnrealEd", "Slate", "SlateCore", "InputCore", "ToolMenus", "PropertyEditor",
			"ContentBrowser", "AssetTools", "AssetRegistry", "EditorFramework", "EditorSubsystem", "MeshDescription",
			"StaticMeshDescription", "LevelEditor", "WorkspaceMenuStructure", "UMG", "ImageCore", "RenderCore", "PythonScriptPlugin", "Projects", "Json" });
	}
}
