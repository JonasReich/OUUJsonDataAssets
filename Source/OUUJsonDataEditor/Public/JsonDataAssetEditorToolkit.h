// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "Toolkits/SimpleAssetEditor.h"

class OUUJSONDATAEDITOR_API FJsonDataAssetEditorToolkit : public FSimpleAssetEditor
{
private:
	using Super = FSimpleAssetEditor;

public:
	static TSharedRef<FJsonDataAssetEditorToolkit> CreateEditor(
		const EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		const TArray<UObject*>& ObjectsToEdit);

	// - IToolkit
	bool IsSimpleAssetEditor() const override { return false; }
	// --

private:
	void ExtendToolBar();
};
