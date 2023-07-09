// Copyright (c) 2023 Jonas Reich & Contributors

using UnrealBuildTool;

public class OUUJsonDataModuleRules : ModuleRules
{
	public OUUJsonDataModuleRules(ReadOnlyTargetRules Target) : base(Target)
	{
		// Disable PCHs for debug configs to ensure the plugin modules are self-contained and stick to IWYU
		PCHUsage = Target.Configuration == UnrealTargetConfiguration.DebugGame
			? ModuleRules.PCHUsageMode.NoPCHs
			: ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		IWYUSupport = IWYUSupport.Full;

		// Also disable unity builds.
		// Unfortunately even this is needed to ensure that all includes are correct when building the plugin by itself.
		bUseUnity = false;
	}
}

public class OUUJsonDataRuntime : OUUJsonDataModuleRules
{
	public OUUJsonDataRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {

			// Engine
			"CoreUObject",
			"Engine",
			"Core",
			"SlateCore",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {

			// Engine
			"Slate",
			"JsonUtilities",
			"Json",
			"Projects"
		});

		// - Editor only dependencies
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"PropertyEditor",
				"SourceControl",
				"AssetTools",

				"ContentBrowser",
				"ContentBrowserData",
				"AssetRegistry"
			});
		}
		// --
	}
}