// Copyright (c) 2023 Jonas Reich & Contributors

#include "CoreMinimal.h"

#include "AssetTypeActionsJsonDataAsset.h"
#include "ContentBrowserJsonDataSource.h"
#include "Interfaces/IPluginManager.h"
#include "JsonAssetReferenceFilter.h"
#include "JsonDataAsset.h"
#include "JsonDataAssetPathDetailsCustomization.h"
#include "LogJsonDataAsset.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"

class FOUUJsonDataEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda([]() {
#if UE_VERSION_NEWER_THAN(5, 2, 999)
			COMPILE_ERROR("Asset reference filter only implemented for 5.2. Please review this code and check if it's "
						  "now possible to bind an asset referencing filter without breaking previously registered "
						  "filters like FDomainAssetReferenceFilter.")
#else
			// This is the only plugin in 5.1 that can conflict with our code.
			// Needs to be reviewed for future engine versions!
			auto AssetReferenceRestrictionsPlugin = IPluginManager::Get().FindPlugin("AssetReferenceRestrictions");
			if (AssetReferenceRestrictionsPlugin->IsEnabled())
			{
				UE_LOG(
					LogJsonDataAsset,
					Warning,
					TEXT("AssetReferenceRestrictions plugin is enabled which prevents registering the "
						 "FJsonAssetReferenceFilter!"))
			}
			else
			{
				GEditor->OnMakeAssetReferenceFilter().BindLambda(
					[](const FAssetReferenceFilterContext& Context) -> TSharedPtr<IAssetReferenceFilter> {
						return MakeShared<FJsonAssetReferenceFilter>(Context);
					});
			}
#endif
		});

		ContentBrowserJsonDataSource = MakeUnique<FContentBrowserJsonDataSource>();

		IAssetTools::Get().RegisterAssetTypeActions(MakeShared<FAssetTypeActions_JsonDataAsset>());
	}

	virtual void ShutdownModule() override { ContentBrowserJsonDataSource.Reset(); }

private:
	TUniquePtr<FContentBrowserJsonDataSource> ContentBrowserJsonDataSource;
};

IMPLEMENT_MODULE(FOUUJsonDataEditorModule, OUUJsonDataEditor)
