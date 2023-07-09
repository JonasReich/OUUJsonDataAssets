// Copyright (c) 2023 Jonas Reich & Contributors

using UnrealBuildTool;

public class OUUJsonDataTests : OUUJsonDataModuleRules
{
	public OUUJsonDataTests(ReadOnlyTargetRules Target) : base(Target)
	{
		// No public dependencies: This module does not have any public files.

		PrivateDependencyModuleNames.AddRange(new string[] {

			// Engine
			"Core",
			"CoreUObject",
			"Engine",
			"SlateCore",
			"Json",

			// OUU Plugins
			"OUUJsonDataRuntime",
		});
	}
}
