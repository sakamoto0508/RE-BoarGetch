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
			"StaticMeshDescription", "LevelEditor", "WorkspaceMenuStructure", "UMG", "ImageCore", "RenderCore", "Projects", "Json" });
		// Python Editor Script Plugin には依存しない（任意）。Scan / Patch Level Materials は
		// 実行時に Python モジュールの有無を調べ、"py" コンソールコマンド経由でスクリプトを実行する。
	}
}
