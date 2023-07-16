// Copyright (c) 2023 Jonas Reich & Contributors

#include "AssetTypeActionsJsonDataAsset.h"

#include "JsonDataAssetEditor.h"
#include "JsonDataAssetEditorToolkit.h"
#include "ToolMenuSection.h"

void FAssetTypeActions_JsonDataAsset::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UJsonDataAsset>> DataAssets = GetTypedWeakObjectPtrs<UJsonDataAsset>(InObjects);

	// The source file should have all important context menu actions, so finding the source item is the only option we
	// add.

	Section.AddMenuEntry(
		"JsonDataAsset_NavigateToSource",
		INVTEXT("Browse to Source"),
		INVTEXT("Browses to the source file and selects it in the most recently used Content Browser"),
		FSlateIcon("EditorStyle", "Icons.OpenSourceLocation"),
		FUIAction(FExecuteAction::CreateLambda([DataAssets]() {
			if (DataAssets.Num() > 0)
			{
				TArray<FJsonDataAssetPath> Paths;
				for (auto& Asset : DataAssets)
				{
					if (Asset.IsValid())
					{
						Paths.Add(Asset->GetPath());
					}
				}

				OUU::Editor::JsonData::ContentBrowser_NavigateToSources(Paths);
			}
		})));
}

void FAssetTypeActions_JsonDataAsset::OpenAssetEditor(
	const TArray<UObject*>& InObjects,
	TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	FJsonDataAssetEditorToolkit::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
}
