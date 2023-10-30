// Copyright (c) 2023 Jonas Reich & Contributors

#include "JsonDataAssetEditor.h"

#include "AssetViewUtils.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "IContentBrowserDataModule.h"
#include "IContentBrowserSingleton.h"
#include "JsonDataAsset.h"
#include "JsonDataAssetGlobals.h"
#include "JsonDataAssetSubsystem.h"
#include "LogJsonDataAsset.h"
#include "PackageTools.h"
#include "Settings/EditorLoadingSavingSettings.h"

namespace OUU::Editor::JsonData
{
	void SyncContentBrowserToItems(const TArray<FString>& ItemPaths)
	{
		const auto* ContentBrowserSubsystem = GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>();
		TArray<FContentBrowserItem> Items;
		for (auto& ItemPath : ItemPaths)
		{
			Items.Add(ContentBrowserSubsystem->GetItemAtPath(*ItemPath, EContentBrowserItemTypeFilter::IncludeAll));
		}
		IContentBrowserSingleton::Get().SyncBrowserToItems(Items);
	}

	FJsonDataAssetPath ConvertMountedSourceFilenameToDataAssetPath(const FString& InFilename)
	{
		// Input paths have a format like "/Data/Plugins/OpenUnrealUtilities/NewDataAsset.json"
		FString PackagePath = InFilename;

		// Convert from a source (.json) path to a cache (.uasset) package path
		{
			PackagePath.ReplaceInline(TEXT(".json"), TEXT(""));
			PackagePath.ReplaceInline(
				*OUU::JsonData::Runtime::GetSourceMountPointRoot_Package(OUU::JsonData::Runtime::GameRootName),
				*OUU::JsonData::Runtime::GetCacheMountPointRoot_Package(OUU::JsonData::Runtime::GameRootName));
		}

		FJsonDataAssetPath JsonPath;
		JsonPath.SetPackagePath(PackagePath);
		return JsonPath;
	}

	FString ConvertMountedSourceFilenameToMountedDataAssetFilename(const FString& InSourceFilePath)
	{
		// Otherwise assume it's a folder
		const bool bIsJsonFile = InSourceFilePath.EndsWith(TEXT(".json"));

		if (bIsJsonFile && FPaths::GetBaseFilename(InSourceFilePath).Contains("."))
		{
			UE_CLOG(
				(OUU::JsonData::Runtime::ShouldIgnoreInvalidExtensions() == false),
				LogJsonDataAsset,
				Warning,
				TEXT("'%s' has an invalid extension (only a simple '.json' is permitted)"),
				*InSourceFilePath);

			return "";
		}

		const auto JsonDataPackagePath = ConvertMountedSourceFilenameToDataAssetPath(InSourceFilePath).GetPackagePath();

		FString MountedAssetPath = JsonDataPackagePath;

		if (InSourceFilePath.StartsWith(TEXT("/All")) == false)
		{
			MountedAssetPath.InsertAt(0, TEXT("/All"));
		}

		if (bIsJsonFile)
		{
			MountedAssetPath.Append(TEXT(".")).Append(OUU::JsonData::Runtime::PackageToObjectName(JsonDataPackagePath));
		}

		return MountedAssetPath;
	}

	FContentBrowserItem GetGeneratedAssetContentBrowserItem(const FString& InSourceFilePath)
	{
		const UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
		if (ensure(IsValid(ContentBrowserData)))
		{
			// Redirect to asset (e.g. format
			// "/All/JsonData/Plugins/OpenUnrealUtilities/Tests/TestAsset_AllValuesSet.TestAsset_AllValuesSet")
			const auto MountedAssetPath = ConvertMountedSourceFilenameToMountedDataAssetFilename(InSourceFilePath);

			auto AssetItem =
				ContentBrowserData->GetItemAtPath(*MountedAssetPath, EContentBrowserItemTypeFilter::IncludeFiles);
			return AssetItem;
		}
		return FContentBrowserItem();
	}

	FContentBrowserItem GetGeneratedAssetContentBrowserItem(const FContentBrowserItem& InSourceContentBrowserItem)
	{
		return GetGeneratedAssetContentBrowserItem(InSourceContentBrowserItem.GetInternalPath().ToString());
	}

	void PerformDiff(const FJsonDataAssetPath& Old, const FJsonDataAssetPath& New)
	{
		const FString OldTextFilename =
			OUU::JsonData::Runtime::PackageToSourceFull(Old.GetPackagePath(), EJsonDataAccessMode::Read);
		const FString NewTextFilename =
			OUU::JsonData::Runtime::PackageToSourceFull(New.GetPackagePath(), EJsonDataAccessMode::Read);
		const FString DiffCommand = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;

		// ReSharper disable once CppExpressionWithoutSideEffects
		IAssetTools::Get().CreateDiffProcess(DiffCommand, OldTextFilename, NewTextFilename);
	}

	void Reload(const FJsonDataAssetPath& Path)
	{
		const auto PackagePath = Path.GetPackagePath();
		FString PackageFilename;
		const bool bPackageAlreadyExists = FPackageName::DoesPackageExist(PackagePath, &PackageFilename);
		const auto* JsonAsset = Path.ForceReload();
		// If called on JsonAssets that do not have a source file anymore JsonAsset result may be null
		if (bPackageAlreadyExists && IsValid(JsonAsset))
		{
			FText ErrorMessage;
			UPackageTools::ReloadPackages(
				TArray<UPackage*>{JsonAsset->GetPackage()},
				OUT ErrorMessage,
				EReloadPackagesInteractionMode::Interactive);
		}
	}

	TAutoConsoleVariable<FString> CVar_Temp(TEXT("ouu.JsonData.SyncPath.Temp"), TEXT(""), TEXT(""));

	void ContentBrowser_NavigateToUAssets(const TArray<FJsonDataAssetPath>& Paths)
	{
		TArray<FString> PathStrings;
		for (auto Path : Paths)
		{
			auto PackagePath = Path.GetPackagePath();
			auto ObjectName = OUU::JsonData::Runtime::PackageToObjectName(PackagePath);
			FString ContentBrowserItemPath = FString::Printf(TEXT("/All%s.%s"), *PackagePath, *ObjectName);
			PathStrings.Add(ContentBrowserItemPath);
		}

		SyncContentBrowserToItems(PathStrings);
	}

	void ContentBrowser_NavigateToSources(const TArray<FJsonDataAssetPath>& Paths)
	{
		TArray<FString> PathStrings;
		for (auto Path : Paths)
		{
			auto PackagePath = Path.GetPackagePath();
			auto RootName = UJsonDataAssetSubsystem::Get().GetRootNameForPackagePath(PackagePath);

			PackagePath.ReplaceInline(
				*OUU::JsonData::Runtime::GetCacheMountPointRoot_Package(RootName),
				*OUU::JsonData::Runtime::GetSourceMountPointRoot_Package(RootName));
			FString ContentBrowserItemPath = FString::Printf(TEXT("/All%s.json"), *PackagePath);
			PathStrings.Add(ContentBrowserItemPath);
		}
		SyncContentBrowserToItems(PathStrings);
	}

	void ContentBrowser_OpenUnrealEditor(const FJsonDataAssetPath& Path)
	{
		if (auto* JsonObject = Path.LoadSynchronous())
		{
			AssetViewUtils::OpenEditorForAsset(JsonObject);
		}
		else
		{
			UE_JSON_DATA_MESSAGELOG(Error, nullptr, TEXT("Failed to load json data asset %s"), *Path.GetPackagePath());
		}
	}
	void ContentBrowser_OpenExternalEditor(const FJsonDataAssetPath& Path)
	{
		const auto DiskPath = OUU::JsonData::Runtime::PackageToSourceFull(Path.GetPackagePath(), EJsonDataAccessMode::Read);
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*DiskPath, nullptr, ELaunchVerb::Edit);
	}
} // namespace OUU::Editor::JsonData
