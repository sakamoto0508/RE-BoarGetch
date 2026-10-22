using UnrealBuildTool;

public class RetroRender : ModuleRules
{
	public RetroRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new[] { "RHI", "UMG", "Slate", "SlateCore", "InputCore" });
	}
}
