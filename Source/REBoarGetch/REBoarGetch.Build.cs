// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class REBoarGetch : ModuleRules
{
	public REBoarGetch(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Retro rendering lives in Plugins/RetroRender.

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"EngineSettings",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"Niagara",
			"EngineCameras",
			// 通常TPSカメラ（UBoarGameplayCameraComponent）と演出のACF連携で使用します。
			"ActionCameraFramework",
			// Level遷移の演出（UBoarLevelTransitionLibrary）で使用します。
			"ActionCameraFrameworkTransitions",
			// 遷移の演出の設定（UBoarLevelTransitionSettings）で使用します。
			"DeveloperSettings"
		});

		// Editor専用の自動化関数（UBoarUIAuthoringLibrary）だけが使用します。
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "UMGEditor", "BlueprintGraph","MovieScene", "MovieSceneTracks" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"REBoarGetch",
			"REBoarGetch/Private/Core",
			"REBoarGetch/Private/Player",
			"REBoarGetch/Private/Boar",
			"REBoarGetch/Private/Gadget",
			"REBoarGetch/Private/Cage",
			"REBoarGetch/Private/Save"
		});
	}
}
