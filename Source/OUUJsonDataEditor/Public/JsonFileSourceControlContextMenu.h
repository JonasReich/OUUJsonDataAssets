// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "ContentBrowserDataMenuContexts.h"

class FContentBrowserFileItemDataPayload;
class UToolMenu;
class SWidget;

class FJsonFileSourceControlContextMenu : public TSharedFromThis<FJsonFileSourceControlContextMenu>
{
public:
	/** Makes the context menu widget */
	void MakeContextMenu(
		UToolMenu* InMenu,
		const TArray<TSharedRef<const FContentBrowserFileItemDataPayload>>& InSelectedFiles);

private:
	void AddMenuOptions(UToolMenu* Menu);
	void AddSourceControlMenuOptions(UToolMenu* Menu);
	void FillSourceControlSubMenu(UToolMenu* Menu);

	void CacheCanExecuteVars();
	bool CanExecuteSourceControlActions() const;
	bool CanExecuteSCCRefresh() const;
	bool CanExecuteDiffSelected() const;

	void ExecuteEnableSourceControl();
	void ExecuteSCCRefresh() const;
	void ExecuteDiffSelected() const;
	void ExecuteSCCCheckOut() const;
	void ExecuteSCCOpenForAdd() const;
	void ExecuteSCCCheckIn() const;
	void ExecuteSCCHistory() const;
	void ExecuteSCCDiffAgainstDepot() const;
	void ExecuteSCCRevert() const;
	void ExecuteSCCSync() const;

	void GetSelectedFileNames(TArray<FString>& OutFileNames) const;

private:
	TArray<TSharedRef<const FContentBrowserFileItemDataPayload>> SelectedAssets;

	TWeakPtr<SWidget> ParentWidget;
	UContentBrowserDataMenuContext_FileMenu::FOnRefreshView OnRefreshView;

	bool bCanExecuteSCCCheckOut = false;
	bool bCanExecuteSCCOpenForAdd = false;
	bool bCanExecuteSCCCheckIn = false;
	bool bCanExecuteSCCHistory = false;
	bool bCanExecuteSCCRevert = false;
	bool bCanExecuteSCCSync = false;
};
