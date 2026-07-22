// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class REBoarGetch : ModuleRules
{
	public REBoarGetch(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PublicIncludePaths.AddRange(new string[] {
			"REBoarGetch",
			"REBoarGetch/Variant_Platforming",
			"REBoarGetch/Variant_Platforming/Animation",
			"REBoarGetch/Variant_Combat",
			"REBoarGetch/Variant_Combat/AI",
			"REBoarGetch/Variant_Combat/Animation",
			"REBoarGetch/Variant_Combat/Gameplay",
			"REBoarGetch/Variant_Combat/Interfaces",
			"REBoarGetch/Variant_Combat/UI",
			"REBoarGetch/Variant_SideScrolling",
			"REBoarGetch/Variant_SideScrolling/AI",
			"REBoarGetch/Variant_SideScrolling/Gameplay",
			"REBoarGetch/Variant_SideScrolling/Interfaces",
			"REBoarGetch/Variant_SideScrolling/UI",
			"REBoarGetch/Private/Core",
			"REBoarGetch/Private/Player",
			"REBoarGetch/Private/Boar",
			"REBoarGetch/Private/Gadget",
			"REBoarGetch/Private/Cage",
			"REBoarGetch/Private/Mission",
			"REBoarGetch/Private/Save"
		});
	}
}
