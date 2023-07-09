// Copyright (c) 2023 Jonas Reich & Contributors

using UnrealBuildTool;

public class OUUJsonDataEditor : OUUJsonDataModuleRules
{
	public OUUJsonDataEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] {

			// Engine
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"EditorStyle",
			"EditorWidgets",
			"EditorSubsystem",
			"Projects",
			"PropertyEditor",
			"ContentBrowser",
			"ContentBrowserFileDataSource",
			"ContentBrowserData",
			"ToolMenus",
			"SourceControlWindows",
			"AssetTools",
			"SourceControl",
			"AssetRegistry",
			"Projects",
			"DataValidation",

			// OUU Plugins
			"OUUJsonDataRuntime"
		});
	}
}
