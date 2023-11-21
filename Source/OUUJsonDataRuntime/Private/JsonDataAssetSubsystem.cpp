// Copyright (c) 2023 Jonas Reich & Contributors

#include "JsonDataAssetSubsystem.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "GameDelegates.h"
#include "Interfaces/IPluginManager.h"
#include "JsonDataAsset.h"
#include "JsonDataAssetConsoleVariables.h"
#include "JsonDataAssetGlobals.h"
#include "JsonDataCacheVersion.h"
#include "JsonLibrary.h"
#include "LogJsonDataAsset.h"
#include "OUUJsonDataRuntimeVersion.h"
#include "UObject/SavePackage.h"

#if WITH_EDITOR
	#include "Editor.h"
#endif

//---------------------------------------------------------------------------------------------------------------------

namespace OUU::JsonData::Runtime
{
	const FName GameRootName = TEXT("Game");

	bool ShouldUseSeparateSourceMountRoot() { return Private::CVar_SeparateSourceMountRoot.GetValueOnAnyThread(); }

	FString GetSourceRoot_ProjectRelative(
		const FName& RootName,
		EJsonDataAccessMode AccessMode,
		bool bOverrideUseUncooked = false)
	{
		const auto& SourceMappings = bOverrideUseUncooked
			? UJsonDataAssetSubsystem::Get().GetSourceMappings(false) // false = uncooked
			: UJsonDataAssetSubsystem::Get().GetSourceMappings(AccessMode);

		if (auto* SourceFull = SourceMappings.Find(RootName))
		{
			return *SourceFull;
		}
		ensure(false);
		return "";
	}

	FString GetSourceRoot_Full(const FName& RootName, EJsonDataAccessMode AccessMode)
	{
		auto Path = FPaths::ProjectDir() / GetSourceRoot_ProjectRelative(RootName, AccessMode);
		return Path;
	}

#if WITH_EDITOR
	// The content browser should always display the state of the uncooked source files
	FString GetSourceMountPointRoot_Package(const FName& RootName)
	{
		FString VirtualRoot = UJsonDataAssetSubsystem::Get().GetVirtualRoot(RootName);
		if (ShouldUseSeparateSourceMountRoot())
		{
			auto MountPoint =
				VirtualRoot.Replace(TEXT("/JsonData/"), *FString(TEXT("/") + Private::GDataSource_Uncooked));
			return MountPoint;
		}
		return VirtualRoot;
	}
	FString GetSourceMountPointRoot_DiskFull(const FName& RootName)
	{
		auto Path = FPaths::ProjectDir() / GetSourceRoot_ProjectRelative(RootName, EJsonDataAccessMode::Read, true);
		return Path;
	}
#endif

	FString GetCacheMountPointRoot_Package(const FName& RootName)
	{
		return UJsonDataAssetSubsystem::Get().GetVirtualRoot(RootName);
	}

	FString GetCacheDir_DiskFull() { return FPaths::ProjectSavedDir() / TEXT("JsonDataCache"); }

	FString GetCacheMountPointRoot_DiskFull(const FName& RootName)
	{
		// Save into Save dir, so the packages are not versioned and can safely be deleted on engine startup.
		auto Path = GetCacheDir_DiskFull() / RootName.ToString();
		return Path;
	}

	bool PackageIsJsonData(const FString& PackagePath) { return PackagePath.StartsWith(TEXT("/JsonData/")); }

	FString PackageToDataRelative(const FString& PackagePath)
	{
		const auto RootName = UJsonDataAssetSubsystem::Get().GetRootNameForPackagePath(PackagePath);
		return PackagePath.Replace(*GetCacheMountPointRoot_Package(RootName), TEXT("")).Append(TEXT(".json"));
	}

	FString PackageToSourceFull(const FString& PackagePath, EJsonDataAccessMode AccessMode)
	{
		const auto RootName = UJsonDataAssetSubsystem::Get().GetRootNameForPackagePath(PackagePath);
		const auto Path = FPaths::ProjectDir() / GetSourceRoot_ProjectRelative(RootName, AccessMode)
			/ PackageToDataRelative(PackagePath);
		return FPaths::ConvertRelativePathToFull(Path);
	}

	// Take a path that is relative to the project root and convert it into a package path.
	FString SourceFullToPackage(const FString& FullPath, EJsonDataAccessMode AccessMode)
	{
		const auto RootName = UJsonDataAssetSubsystem::Get().GetRootNameForSourcePath(FullPath);

		auto RelativeToSource = FullPath;
		ensure(FPaths::MakePathRelativeTo(
			RelativeToSource,
			*FString(GetSourceRoot_Full(RootName, AccessMode) + TEXT("/"))));

		return GetCacheMountPointRoot_Package(RootName) / RelativeToSource.Replace(TEXT(".json"), TEXT(""));
	}

	FString PackageToObjectName(const FString& Package)
	{
		int32 Idx = INDEX_NONE;
		if (!Package.FindLastChar('/', OUT Idx))
			return "";
		return Package.RightChop(Idx + 1);
	}

	bool ShouldIgnoreInvalidExtensions()
	{
		return OUU::JsonData::Runtime::Private::CVar_IgnoreInvalidExtensions.GetValueOnAnyThread();
	}

	bool ShouldReadFromCookedContent()
	{
#if WITH_EDITOR
		// Are there cases where we want to read from cooked content in editor? E.g. when running "-game" with
		// cooked content?
		return false;
#else
		return true;
#endif
	}

	bool ShouldWriteToCookedContent()
	{
#if WITH_EDITOR
		// Are there other cases? E.g. when running "-game" with cooked content?
		return IsRunningCookCommandlet();
#else
		return true;
#endif
	}

	void CheckJsonPaths()
	{
#if DO_CHECK
		auto CheckJsonPathFromConfig = [](const FString& Path) {
			if (Path.StartsWith(TEXT("/")))
			{
				UE_LOG(LogJsonDataAsset, Fatal, TEXT("Json path '%s' must not begin with a slash"), *Path);
			}
			if (Path.Contains(TEXT("//")))
			{
				UE_LOG(LogJsonDataAsset, Fatal, TEXT("Json path '%s' must not contain double slashes"), *Path);
			}
			if (Path.EndsWith(TEXT("/")) == false)
			{
				UE_LOG(LogJsonDataAsset, Fatal, TEXT("Json path '%s' does not end in a single slash"), *Path);
			}
		};
		CheckJsonPathFromConfig(OUU::JsonData::Runtime::Private::GDataSource_Uncooked);
		CheckJsonPathFromConfig(OUU::JsonData::Runtime::Private::GDataSource_Cooked);

		if (FString::Printf(TEXT("/%s"), *OUU::JsonData::Runtime::Private::GDataSource_Uncooked)
			== OUU::JsonData::Runtime::GetCacheMountPointRoot_Package(OUU::JsonData::Runtime::GameRootName))
		{
			UE_LOG(
				LogJsonDataAsset,
				Fatal,
				TEXT("Json Data source directory '%s' must have a different name than asset mount point '%s'"),
				*OUU::JsonData::Runtime::Private::GDataSource_Uncooked,
				*FString(OUU::JsonData::Runtime::GetCacheMountPointRoot_Package(OUU::JsonData::Runtime::GameRootName)));
		}

		if (OUU::JsonData::Runtime::Private::GDataSource_Uncooked
			== OUU::JsonData::Runtime::Private::GDataSource_Cooked)
		{
			UE_LOG(
				LogJsonDataAsset,
				Fatal,
				TEXT("Cooked and uncooked json paths must differ (%s, %s)"),
				*OUU::JsonData::Runtime::Private::GDataSource_Cooked,
				*OUU::JsonData::Runtime::Private::GDataSource_Uncooked)
		}
#endif
	}

} // namespace OUU::JsonData::Runtime

bool FJsonDataAssetMetaDataCache::SaveToFile(const FString& FilePath) const
{
	const auto MetaDataCacheJsonObject = UOUUJsonLibrary::UStructToJsonObject(this, {}, 0, 0, false);
	if (MetaDataCacheJsonObject.IsValid() == false)
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to create Json object from meta data cache."));
		return false;
	}

	FString JsonString;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(OUT & JsonString);
	if (FJsonSerializer::Serialize(MetaDataCacheJsonObject.ToSharedRef(), JsonWriter) == false)
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to serialize Json asset meta data cache."));
		return false;
	}

	if (FFileHelper::SaveStringToFile(JsonString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8) == false)
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to save Json asset meta data cache to file %s."), *FilePath);
		return false;
	}

	return true;
}

bool FJsonDataAssetMetaDataCache::LoadFromFile(const FString& FilePath)
{
	FString JsonString;
	if (FFileHelper::LoadFileToString(JsonString, *FilePath) == false)
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to load Json asset meta data cache from file %s."), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject) == false || JsonObject.IsValid() == false)
	{
		UE_LOG(
			LogJsonDataAsset,
			Error,
			TEXT("Failed to deserialize Json asset meta data cache from file %s."),
			*FilePath);
		return false;
	}

	FArchive VersionLoadingArchive;
	VersionLoadingArchive.SetIsLoading(true);
	VersionLoadingArchive.SetIsPersistent(true);
	if (UOUUJsonLibrary::JsonObjectToUStruct(
			JsonObject.ToSharedRef(),
			FJsonDataAssetMetaDataCache::StaticStruct(),
			this,
			VersionLoadingArchive)
		== false)
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to load Json asset meta data cache from Json object."));
		return false;
	}

	return true;
}

void UJsonDataAssetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Don't add to plugin root names!!!
	// AllPluginRootNames.Add();
	AllRootNames.Add(OUU::JsonData::Runtime::GameRootName);

	{
		// #TODO-OUU Remove uncooked lookup in cooked build?
		const auto UncookedSourceDir = OUU::JsonData::Runtime::Private::GDataSource_Uncooked;
		AllSourceDirectories_Uncooked.Add(UncookedSourceDir);
		SourceDirectories_Uncooked.Add(OUU::JsonData::Runtime::GameRootName, UncookedSourceDir);
	}
	{
		const auto CookedSourceDir = OUU::JsonData::Runtime::Private::GDataSource_Cooked;
		AllSourceDirectories_Cooked.Add(CookedSourceDir);
		SourceDirectories_Cooked.Add(OUU::JsonData::Runtime::GameRootName, CookedSourceDir);
	}

	OUU::JsonData::Runtime::CheckJsonPaths();

	RegisterMountPoints(OUU::JsonData::Runtime::GameRootName);

	bAutoExportJson = true;

	FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddUObject(this, &UJsonDataAssetSubsystem::PostEngineInit);

#if WITH_EDITOR
	FEditorDelegates::OnPackageDeleted.AddUObject(this, &UJsonDataAssetSubsystem::HandlePackageDeleted);
	FEditorDelegates::PreBeginPIE.AddUObject(this, &UJsonDataAssetSubsystem::HandlePreBeginPIE);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// I know, disabling deprecation warnings is shit, but this is easy to fix when it eventually breaks.
	// And having it delegate based is cleaner (and hopefully possible in 5.2) via ModifyCookDelegate,
	// which seems to be accidentally left private in 5.0 / 5.1
	// ReSharper disable once CppDeprecatedEntity
	FGameDelegates::Get().GetCookModificationDelegate().BindUObject(this, &UJsonDataAssetSubsystem::ModifyCook);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
	// In non-editor builds, load Json asset meta data cache file.
	AssetMetaDataCache.LoadFromFile(GetMetaDataCacheFilePath(EJsonDataAccessMode::Read));
#endif
}

void UJsonDataAssetSubsystem::Deinitialize()
{
	Super::Deinitialize();

	UnregisterMountPoints(OUU::JsonData::Runtime::GameRootName);

	bAutoExportJson = false;

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

#if WITH_EDITOR
	FEditorDelegates::OnPackageDeleted.RemoveAll(this);
	FEditorDelegates::PreBeginPIE.RemoveAll(this);
#endif
}

bool UJsonDataAssetSubsystem::AutoExportJsonEnabled()
{
	if (IsRunningCookCommandlet())
		return false;
	return GEngine->GetEngineSubsystem<UJsonDataAssetSubsystem>()->bAutoExportJson;
}

void UJsonDataAssetSubsystem::NetSerializePath(FJsonDataAssetPath& Path, FArchive& Ar)
{
	UJsonDataAssetSubsystem* SubsystemInstance = nullptr;
	if (GEngine && GEngine->IsInitialized())
	{
		SubsystemInstance = GEngine->GetEngineSubsystem<UJsonDataAssetSubsystem>();
	}

	auto SoftObjectPath = Path.Path.ToSoftObjectPath();

	bool bHasPath = SoftObjectPath.IsNull() == false;
	Ar.SerializeBits(&bHasPath, 1);

	if (bHasPath)
	{
		bool bUsesFastSerialization =
			OUU::JsonData::Runtime::Private::CVar_UseFastNetSerialization.GetValueOnGameThread() && SubsystemInstance
			&& SubsystemInstance->bJsonDataAssetListBuilt;
		int32 PathIndex = 0;
		if (bUsesFastSerialization && Ar.IsSaving())
		{
			const int32* OptIndex =
				SubsystemInstance->AllJsonDataAssetsByPath.Find(SoftObjectPath.GetLongPackageFName());
			if (ensureMsgf(
					OptIndex,
					TEXT("Tried to NetSerialize json data asset path '%s' which does not appear to exist."),
					*SoftObjectPath.ToString()))
			{
				PathIndex = *OptIndex;
			}
			else
			{
				bUsesFastSerialization = false;
			}
		}

		Ar.SerializeBits(&bUsesFastSerialization, 1);

		if (bUsesFastSerialization)
		{
			checkf(
				(SubsystemInstance && SubsystemInstance->bJsonDataAssetListBuilt) || Ar.IsLoading() == false,
				TEXT("Received json data asset path using fast net serialization, but our asset list has not been "
					 "built!"));
			Ar.SerializeBits(&PathIndex, SubsystemInstance->PathIndexNetSerializeBits);

			const auto GetPackageNameFromPath = [](const FName& _Path) -> FName {
				int32 NameStartIndex;
				const auto& PathString = _Path.ToString();
				if (PathString.FindLastChar(TEXT('/'), NameStartIndex))
				{
					return FName(PathString.RightChop(NameStartIndex + 1));
				}

				return NAME_None;
			};

			// Usually, the asset name matches the package name so we do not need to serialize it separately
			bool bAssetNameMatchesPackage = false;
			FName AssetName;
			if (Ar.IsSaving())
			{
				AssetName = GetPackageNameFromPath(SoftObjectPath.GetLongPackageFName());
				bAssetNameMatchesPackage = AssetName == SoftObjectPath.GetAssetFName();
			}

			Ar.SerializeBits(&bAssetNameMatchesPackage, 1);

			if (bAssetNameMatchesPackage == false)
			{
				Ar << AssetName;
			}

			// Serialize subobject path if we have it. In most cases this will be empty.
			bool bHasSubObjectPath = SoftObjectPath.GetSubPathString().IsEmpty() == false;
			Ar.SerializeBits(&bHasSubObjectPath, 1);

			FString SubObjectPath;
			if (bHasSubObjectPath)
			{
				if (Ar.IsSaving())
				{
					SubObjectPath = SoftObjectPath.GetSubPathString();
				}

				Ar << SubObjectPath;
			}

			if (Ar.IsLoading())
			{
				if (ensureMsgf(
						PathIndex < SubsystemInstance->AllJsonDataAssetsByIndex.Num(),
						TEXT("Received out-of range json data asset path index %i!"),
						PathIndex))
				{
					const auto& PackagePath = SubsystemInstance->AllJsonDataAssetsByIndex[PathIndex];
					if (bAssetNameMatchesPackage)
					{
						AssetName = GetPackageNameFromPath(PackagePath);
					}

					// Construct path from the pieces we have gathered.
					SoftObjectPath = FSoftObjectPath(PackagePath, AssetName, SubObjectPath);
				}
				else
				{
					SoftObjectPath.Reset();
				}
			}
		}
		else
		{
			Ar << SoftObjectPath;
		}

		if (Ar.IsLoading())
		{
			Path.Path = MoveTemp(SoftObjectPath);
		}
	}
	else if (Ar.IsLoading())
	{
		Path.Path.Reset();
	}
}

void UJsonDataAssetSubsystem::ImportAllAssets(bool bOnlyMissing)
{
	if (bJsonDataAssetListBuilt == false)
	{
		RescanAllAssets();
	}

	const bool bIgnoreErrorsDuringImport =
		OUU::JsonData::Runtime::Private::CVar_IgnoreLoadErrorsDuringStartupImport.GetValueOnAnyThread();

	if (bIgnoreErrorsDuringImport)
	{
		// We have to ignore references to generated json packages while doing the initial import.
		// Json assets might reference each other.
		for (auto& PackageName : AllJsonDataAssetsByIndex)
		{
			FLinkerLoad::AddKnownMissingPackage(*PackageName.ToString());
		}
	}

	// Perform the actual import
	for (auto& RootName : AllRootNames)
	{
		ImportAllAssets(RootName, bOnlyMissing);
	}

	if (bIgnoreErrorsDuringImport)
	{
		// Now any further package load errors are valid
		for (auto& PackageName : AllJsonDataAssetsByIndex)
		{
			FLinkerLoad::RemoveKnownMissingPackage(*PackageName.ToString());
		}
	}

	bIsInitialAssetImportCompleted = true;
}

void UJsonDataAssetSubsystem::RescanAllAssets()
{
	AllJsonDataAssetsByIndex.Empty();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	for (auto& RootName : AllRootNames)
	{
		auto SourceRoot = OUU::JsonData::Runtime::GetSourceRoot_Full(RootName, EJsonDataAccessMode::Read);
		PlatformFile.IterateDirectoryRecursively(*SourceRoot, [&](const TCHAR* FilePath, bool bIsDirectory) -> bool {
			if (bIsDirectory == false)
			{
				const auto PackagePath =
					OUU::JsonData::Runtime::SourceFullToPackage(FilePath, EJsonDataAccessMode::Read);
				AllJsonDataAssetsByIndex.Add(FName(PackagePath));
			}
			return true;
		});
	}

	// Note: We sort using LexicalLess here instead of FastLess, because we need the resulting order to be deterministic
	// across multiple clients.
	AllJsonDataAssetsByIndex.Sort([](const FName& _A, const FName& _B) { return _A.LexicalLess(_B); });

	const int32 NumPaths = AllJsonDataAssetsByIndex.Num();
	AllJsonDataAssetsByPath.Empty(NumPaths);
	for (int32 i = 0; i < NumPaths; ++i)
	{
		AllJsonDataAssetsByPath.Add(AllJsonDataAssetsByIndex[i], i);
	}

	PathIndexNetSerializeBits = 0;
	const int32 MaxPathIndex = FMath::Max(0, NumPaths - 1);
	for (int32 i = 0; i < 32; ++i)
	{
		if (MaxPathIndex & (1 << i))
		{
			PathIndexNetSerializeBits = i + 1ll;
		}
	}

	bJsonDataAssetListBuilt = true;
}

TArray<FJsonDataAssetPath> UJsonDataAssetSubsystem::GetJsonAssetsByClass(
	TSoftClassPtr<UJsonDataAsset> Class,
	const bool bSearchSubClasses) const
{
	const auto& AssetRegistry = IAssetRegistry::GetChecked();
	const FTopLevelAssetPath ClassAssetPath(Class.ToString());
	TArray<FJsonDataAssetPath> Results;

#if WITH_EDITOR
	// In the editor, we can simply use the asset registry to find our data.
	TArray<FAssetData> AssetData;
	AssetRegistry.GetAssetsByClass(ClassAssetPath, AssetData, bSearchSubClasses);

	Results.Reserve(AssetData.Num());
	for (const auto& Data : AssetData)
	{
		Results.Add(FJsonDataAssetPath::FromSoftObjectPath(Data.GetSoftObjectPath()));
	}
#else
	// Outside of that, we need to use our own cache as the generated .uassets are not included in cooked builds.
	TSet<FTopLevelAssetPath> DerivedClassNames;
	AssetRegistry.GetDerivedClassNames({ClassAssetPath}, {}, DerivedClassNames);

	for (const auto& ClassPath : DerivedClassNames)
	{
		const auto pEntries = AssetMetaDataCache.PathsByClass.Find(ClassPath);
		if (pEntries)
		{
			Results.Reserve(Results.Num() + pEntries->Paths.Num());
			for (const auto& Path : pEntries->Paths)
			{
				Results.Add(Path);
			}
		}
	}
#endif

	return Results;
}

void UJsonDataAssetSubsystem::ImportAllAssets(const FName& RootName, bool bOnlyMissing)
{
	const FString JsonDir = OUU::JsonData::Runtime::GetSourceRoot_Full(RootName, EJsonDataAccessMode::Read);
	if (!FPaths::DirectoryExists(JsonDir))
	{
		// No need to import anything if there is no json source directory
		return;
	}

	// Ensure that none of the asset saves during this call scope cause json exports.
	TGuardValue ScopedDisableAutoExport{this->bAutoExportJson, false};

	TArray<UPackage*> AllPackages;
	int32 NumPackagesLoaded = 0;
	int32 NumPackagesFailedToLoad = 0;
	auto VisitorLambda =
		[&NumPackagesLoaded, &NumPackagesFailedToLoad, bOnlyMissing](const TCHAR* FilePath, bool bIsDirectory) -> bool {
		if (bIsDirectory)
			return true;

		if (FPaths::GetBaseFilename(FilePath).Contains(".") || FPaths::GetExtension(FilePath) != TEXT("json"))
		{
			if (!OUU::JsonData::Runtime::ShouldIgnoreInvalidExtensions())
			{
				UE_JSON_DATA_MESSAGELOG(
					Warning,
					nullptr,
					TEXT("File %s in Data directory has an unexpected file extension."),
					FilePath);
				NumPackagesFailedToLoad++;
			}
			// Continue with other files anyways
			return true;
		}

		if (FPaths::GetBaseFilename(FilePath).Contains("."))
		{
			if (!OUU::JsonData::Runtime::ShouldIgnoreInvalidExtensions())
			{
				UE_JSON_DATA_MESSAGELOG(
					Warning,
					nullptr,
					TEXT("File %s in Data directory has two '.' characters in it's filename. Only a simple '.json' "
						 "extension is allowed."),
					FilePath);
				NumPackagesFailedToLoad++;
			}
			// Continue with other files anyways
			return true;
		}

		const auto PackagePath = OUU::JsonData::Runtime::SourceFullToPackage(FilePath, EJsonDataAccessMode::Read);

		FString PackageFilename;
		const bool bPackageAlreadyExists = FPackageName::DoesPackageExist(PackagePath, &PackageFilename);
		if (bPackageAlreadyExists && bOnlyMissing)
		{
			// Existing asset was found. Skip if only importing missing files.
			return true;
		}

		auto* NewDataAsset = FJsonDataAssetPath::FromPackagePath(PackagePath).ForceReload();
		if (!IsValid(NewDataAsset))
		{
			// Error messages in the load function itself should be sufficient. But it's nice to have a summary
			// metric.
			NumPackagesFailedToLoad++;
			// Continue with other files anyways
			return true;
		}

#if WITH_EDITOR
		if (GIsEditor)
		{
			UPackage* NewPackage = NewDataAsset->GetPackage();

			// The name of the package
			const FString PackageName = NewPackage->GetName();

			// Construct a filename from long package name.
			const FString& FileExtension = FPackageName::GetAssetPackageExtension();
			PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FileExtension);

			// The file already exists, no need to prompt for save as
			FString BaseFilename, Extension, Directory;
			// Split the path to get the filename without the directory structure
			FPaths::NormalizeFilename(PackageFilename);
			FPaths::Split(PackageFilename, Directory, BaseFilename, Extension);

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.Error = GWarn;
			const auto SaveResult = UPackage::Save(NewPackage, NewDataAsset, *PackageFilename, SaveArgs);

			if (SaveResult == ESavePackageResult::Success)
			{
				NumPackagesLoaded++;
			}
			else
			{
				UE_JSON_DATA_MESSAGELOG(
					Error,
					NewPackage,
					TEXT("Failed to save package for json data asset %s"),
					FilePath);

				NumPackagesFailedToLoad++;
			}
		}
#endif
		return true;
	};

	const IPlatformFile::FDirectoryVisitorFunc VisitorFunc = VisitorLambda;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.IterateDirectoryRecursively(*JsonDir, VisitorFunc);

	UE_LOG(LogJsonDataAsset, Log, TEXT("Loaded %i json data assets"), NumPackagesLoaded);
	UE_CLOG(
		NumPackagesFailedToLoad > 0,
		LogJsonDataAsset,
		Error,
		TEXT("Failed to load %i json data assets"),
		NumPackagesFailedToLoad);

	OUU::JsonData::Runtime::FCacheVersion::Current().Write();
}

void UJsonDataAssetSubsystem::AddPluginDataRoot(const FName& PluginName)
{
	if (AllPluginRootNames.Contains(PluginName))
	{
		UE_LOG(
			LogJsonDataAsset,
			Warning,
			TEXT("Json Data root already registered for plugin %s"),
			*PluginName.ToString());
		return;
	}

	auto& PluginManager = IPluginManager::Get();
	if (const auto Plugin = PluginManager.FindPlugin(*PluginName.ToString()))
	{
		const auto PluginBaseDir = Plugin->GetBaseDir();

		AllPluginRootNames.Add(PluginName);
		AllRootNames.Add(PluginName);
		{
			// Uncooked data files are split per plugin
			auto UncookedSourceDir = PluginBaseDir / OUU::JsonData::Runtime::Private::GDataSource_Uncooked;
			FPaths::MakePathRelativeTo(UncookedSourceDir, *FPaths::ProjectDir());
			AllSourceDirectories_Uncooked.Add(UncookedSourceDir);
			SourceDirectories_Uncooked.Add(PluginName, UncookedSourceDir);
		}
		{
			// Cooked data files should go into a subfolder of the regular cooked data folder for easier packaging /
			// redistribution
			const auto CookedSourceDir =
				OUU::JsonData::Runtime::Private::GDataSource_Cooked / TEXT("Plugins") / PluginName.ToString();
			AllSourceDirectories_Cooked.Add(CookedSourceDir);
			SourceDirectories_Cooked.Add(PluginName, CookedSourceDir);
		}

		RegisterMountPoints(PluginName);

		OnNewPluginRootAdded.Broadcast(PluginName);
	}
	else
	{
		ensureMsgf(
			false,
			TEXT("Plugin %s can't be added as JsonDataAsset root, because it does not exist"),
			*PluginName.ToString());
	}
}

const TMap<FName, FString>& UJsonDataAssetSubsystem::GetSourceMappings(EJsonDataAccessMode AccessMode) const
{
	switch (AccessMode)
	{
	case EJsonDataAccessMode::Read: return GetSourceMappings(OUU::JsonData::Runtime::ShouldReadFromCookedContent());
	case EJsonDataAccessMode::Write: return GetSourceMappings(OUU::JsonData::Runtime::ShouldWriteToCookedContent());
	default: checkf(false, TEXT("Invalid access mode")); return SourceDirectories_Uncooked;
	}
}

const TMap<FName, FString>& UJsonDataAssetSubsystem::GetSourceMappings(bool bUseCookedContent) const
{
	return bUseCookedContent ? SourceDirectories_Cooked : SourceDirectories_Uncooked;
}

const TArray<FString>& UJsonDataAssetSubsystem::GetAllSourceDirectories(EJsonDataAccessMode AccessMode) const
{
	switch (AccessMode)
	{
	case EJsonDataAccessMode::Read:
		return OUU::JsonData::Runtime::ShouldReadFromCookedContent() ? AllSourceDirectories_Cooked
																	 : AllSourceDirectories_Uncooked;
	case EJsonDataAccessMode::Write:
		return OUU::JsonData::Runtime::ShouldWriteToCookedContent() ? AllSourceDirectories_Cooked
																	: AllSourceDirectories_Uncooked;
	default: checkf(false, TEXT("Invalid access mode")); return AllSourceDirectories_Uncooked;
	}
}

FString UJsonDataAssetSubsystem::GetVirtualRoot(const FName& RootName) const
{
	return (RootName == OUU::JsonData::Runtime::GameRootName)
		? TEXT("/JsonData/")
		: FString::Printf(TEXT("/JsonData/Plugins/%s/"), *RootName.ToString());
}

FName UJsonDataAssetSubsystem::GetRootNameForPackagePath(const FString& PackagePath) const
{
	if (PackagePath.StartsWith(TEXT("/JsonData/Plugins/")) == false)
	{
		return OUU::JsonData::Runtime::GameRootName;
	}

	for (auto& RootName : AllPluginRootNames)
	{
		if (PackagePath.StartsWith(GetVirtualRoot(RootName)))
		{
			return RootName;
		}
	}

	return NAME_None;
}

FName UJsonDataAssetSubsystem::GetRootNameForSourcePath(const FString& SourcePath) const
{
	// Assume Game is most common, so test it first
	if (IsPathInSourceDirectoryOfNamedRoot(SourcePath, OUU::JsonData::Runtime::GameRootName))
		return OUU::JsonData::Runtime::GameRootName;

	// Then go through all plugins
	for (auto& PluginName : AllPluginRootNames)
	{
		if (IsPathInSourceDirectoryOfNamedRoot(SourcePath, PluginName))
			return PluginName;
	}

	return NAME_None;
}

bool UJsonDataAssetSubsystem::IsPathInSourceDirectoryOfNamedRoot(const FString& SourcePath, const FName& RootName) const
{
	// Assume cooked is more common (in game runtime)
	if (auto* CookedDir = SourceDirectories_Cooked.Find(RootName))
	{
		if (FPaths::IsUnderDirectory(SourcePath, *(FPaths::ProjectDir() / *CookedDir)))
			return true;
	}

	if (auto* UncookedDir = SourceDirectories_Uncooked.Find(RootName))
	{
		if (FPaths::IsUnderDirectory(SourcePath, *(FPaths::ProjectDir() / *UncookedDir)))
			return true;
	}

	return false;
}

FString UJsonDataAssetSubsystem::GetMetaDataCacheFilePath(const EJsonDataAccessMode AccessMode) const
{
	return OUU::JsonData::Runtime::GetSourceRoot_Full(OUU::JsonData::Runtime::GameRootName, AccessMode)
		/ TEXT("JsonMetaDataCache.json");
}

void UJsonDataAssetSubsystem::RegisterMountPoints(const FName& RootName)
{
#if WITH_EDITOR
	// Make sure that the asset cache is always cleared for new mount roots.
	CleanupAssetCache(RootName);

	if (OUU::JsonData::Runtime::ShouldUseSeparateSourceMountRoot())
	{
		FPackageName::RegisterMountPoint(
			OUU::JsonData::Runtime::GetSourceMountPointRoot_Package(RootName),
			OUU::JsonData::Runtime::GetSourceMountPointRoot_DiskFull(RootName));
	}
#endif

	FPackageName::RegisterMountPoint(
		OUU::JsonData::Runtime::GetCacheMountPointRoot_Package(RootName),
		OUU::JsonData::Runtime::GetCacheMountPointRoot_DiskFull(RootName));
}

void UJsonDataAssetSubsystem::UnregisterMountPoints(const FName& RootName)
{
	FPackageName::UnRegisterMountPoint(
		OUU::JsonData::Runtime::GetCacheMountPointRoot_Package(RootName),
		OUU::JsonData::Runtime::GetCacheMountPointRoot_DiskFull(RootName));

#if WITH_EDITOR
	if (OUU::JsonData::Runtime::ShouldUseSeparateSourceMountRoot())
	{
		FPackageName::UnRegisterMountPoint(
			OUU::JsonData::Runtime::GetSourceMountPointRoot_Package(RootName),
			OUU::JsonData::Runtime::GetSourceMountPointRoot_DiskFull(RootName));
	}
#endif
}

void UJsonDataAssetSubsystem::PostEngineInit()
{
	RescanAllAssets();

	if (OUU::JsonData::Runtime::Private::CVar_ImportAllAssetsOnStartup.GetValueOnGameThread())
	{
		ImportAllAssets(true);
	}
}

#if WITH_EDITOR
void UJsonDataAssetSubsystem::HandlePreBeginPIE(const bool bIsSimulating)
{
	// Make sure all asset paths are up to date in case we want to use fast net serialization.
	RescanAllAssets();
}

void UJsonDataAssetSubsystem::CleanupAssetCache(const FName& RootName)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString MountDiskPath = OUU::JsonData::Runtime::GetCacheMountPointRoot_DiskFull(RootName);
	if (PlatformFile.DirectoryExists(*MountDiskPath) == false)
	{
		// No existing cache
		return;
	}

	const bool bPurgeCVarValue = OUU::JsonData::Runtime::Private::CVar_PurgeAssetCacheOnStartup.GetValueOnGameThread();
	if (bPurgeCVarValue || (OUU::JsonData::Runtime::FCacheVersion::IsCacheCompatible() == false)
		|| IsRunningCookCommandlet())
	{
		UE_LOG(
			LogJsonDataAsset,
			Log,
			TEXT("Purging the entire json data asset cache. Reason: %s"),
			*FString(
				bPurgeCVarValue ? TEXT("Console variable ouu.JsonData.PurgeAssetCacheOnStartup=true")
								: TEXT("CacheVersion marker is incompatible with current editor binaries")));

		// Delete the directory on-disk before mounting the directory to purge all generated uasset files.
		PlatformFile.DeleteDirectoryRecursively(*MountDiskPath);
		return;
	}

	// If clearing the whole cache is disabled, at least stale assets that do not have a corresponding source file must
	// be removed
	auto VisitorLambda = [&PlatformFile, &MountDiskPath, &RootName](const TCHAR* FilePath, bool bIsDirectory) -> bool {
		if (bIsDirectory)
		{
			return true;
		}

		const auto FilePathStr = FStringView(FilePath);
		const auto AssetExtension = FPackageName::GetAssetPackageExtension();
		if (FilePathStr.EndsWith(AssetExtension) == false)
		{
			UE_LOG(
				LogJsonDataAsset,
				Warning,
				TEXT("%s is in json data cache directory, but has unexpected extension. Expected only .uasset "
					 "files."),
				FilePath);
			return true;
		}

		FString RelativePath = FilePath;
		const bool bIsRelative = FPaths::MakePathRelativeTo(IN OUT RelativePath, *FString(MountDiskPath + TEXT("/")));
		ensureMsgf(bIsRelative, TEXT("File path %s must be in subdirectory of %s"), *FilePath, *MountDiskPath);
		ensureMsgf(
			RelativePath.StartsWith(TEXT("./")) == false,
			TEXT("%s is expecteed to be a relative path but not with a './' prefix"),
			*RelativePath);

		RelativePath = RelativePath.Mid(0, RelativePath.Len() - AssetExtension.Len());

		const auto PackagePath = OUU::JsonData::Runtime::GetCacheMountPointRoot_Package(RootName).Append(RelativePath);

		const auto SourcePath = OUU::JsonData::Runtime::PackageToSourceFull(PackagePath, EJsonDataAccessMode::Read);
		if (PlatformFile.FileExists(*SourcePath) == false)
		{
			PlatformFile.DeleteFile(FilePath);
			UE_LOG(
				LogJsonDataAsset,
				Log,
				TEXT("Deleted stale uasset (json source is missing) from json data cache: %s"),
				FilePath);
		}
		else
		{
			const auto AssetModTime = PlatformFile.GetTimeStamp(FilePath);
			const auto SourceModTime = PlatformFile.GetTimeStamp(*SourcePath);

			if (AssetModTime < SourceModTime)
			{
				PlatformFile.DeleteFile(FilePath);
				UE_LOG(
					LogJsonDataAsset,
					Log,
					TEXT("Deleted outdated uasset (json source is newer) from json data cache: %s"),
					FilePath);
			}
		}

		return true;
	};

	PlatformFile.IterateDirectoryRecursively(*MountDiskPath, VisitorLambda);
}

void UJsonDataAssetSubsystem::HandlePackageDeleted(UPackage* Package)
{
	const auto PackagePath = Package->GetPathName();

	if (!OUU::JsonData::Runtime::PackageIsJsonData(PackagePath))
		return;

	OUU::JsonData::Runtime::Private::Delete(PackagePath);
}

void UJsonDataAssetSubsystem::ModifyCook(TArray<FString>& OutExtraPackagesToCook)
{
	IAssetRegistry& AssetRegistry = *IAssetRegistry::Get();
	AssetRegistry.WaitForCompletion();

	ensure(bIsInitialAssetImportCompleted);

	// Delete files from previous cook
	for (auto SourceDir : GetAllSourceDirectories(EJsonDataAccessMode::Write))
	{
		FPlatformFileManager::Get().GetPlatformFile().DeleteDirectoryRecursively(*SourceDir);
	}

	TSet<FName> DependencyPackages;
	FJsonDataAssetMetaDataCache MetaDataCache;
	for (auto& RootName : AllRootNames)
	{
		ModifyCookInternal(RootName, OUT DependencyPackages, OUT MetaDataCache);
	}

	MetaDataCache.SaveToFile(GetMetaDataCacheFilePath(EJsonDataAccessMode::Write));

	for (FName& PackageName : DependencyPackages)
	{
		OutExtraPackagesToCook.Add(PackageName.ToString());
	}

	UE_LOG(
		LogJsonDataAsset,
		Display,
		TEXT("UJsonDataAssetLibrary::ModifyCook - Added %i dependency assets for json assets to cook"),
		DependencyPackages.Num());
}

void UJsonDataAssetSubsystem::ModifyCookInternal(
	const FName& RootName,
	TSet<FName>& OutDependencyPackages,
	FJsonDataAssetMetaDataCache& OutMetaDataCache)
{
	const FString JsonDir_READ = OUU::JsonData::Runtime::GetSourceRoot_Full(RootName, EJsonDataAccessMode::Read);
	if (!FPaths::DirectoryExists(JsonDir_READ))
	{
		return;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IAssetRegistry& AssetRegistry = *IAssetRegistry::Get();

	int32 NumJsonDataAssetsAdded = 0;

	auto VisitorLambda = [&NumJsonDataAssetsAdded,
						  &OutDependencyPackages,
						  &OutMetaDataCache](const TCHAR* FilePath, bool bIsDirectory) -> bool {
		if (bIsDirectory)
			return true;

		if (FPaths::GetExtension(FilePath) != TEXT("json"))
			return true;

		if (OUU::JsonData::Runtime::ShouldIgnoreInvalidExtensions() && FPaths::GetBaseFilename(FilePath).Contains("."))
			return true;

		const auto PackagePath = OUU::JsonData::Runtime::SourceFullToPackage(FilePath, EJsonDataAccessMode::Read);
		const auto Path = FJsonDataAssetPath::FromPackagePath(PackagePath);

		const auto* LoadedJsonDataAsset = Path.LoadSynchronous();
		if (!ensure(LoadedJsonDataAsset))
			return true;

		// ReSharper disable once CppExpressionWithoutSideEffects
		LoadedJsonDataAsset->ExportJsonFile();

		auto& PackagePaths =
			OutMetaDataCache.PathsByClass.FindOrAdd(LoadedJsonDataAsset->GetClass()->GetClassPathName());
		PackagePaths.Paths.Add(Path);

		const auto ObjectName = OUU::JsonData::Runtime::PackageToObjectName(PackagePath);
		const FAssetIdentifier AssetIdentifier(*PackagePath, *ObjectName);

		TArray<FAssetIdentifier> Dependencies;
		IAssetRegistry::Get()->GetDependencies(AssetIdentifier, OUT Dependencies);

		for (auto& Dependency : Dependencies)
		{
			if (Dependency.IsPackage())
			{
				auto PackageName = Dependency.PackageName;
				if (OUU::JsonData::Runtime::PackageIsJsonData(PackageName.ToString()) == false)
				{
					// We don't want to add json data assets directly to this list.
					// As they are on the do not cook list, they will throw errors.
					OutDependencyPackages.Add(PackageName);
				}
			}
		}
		NumJsonDataAssetsAdded += 1;

		return true;
	};

	const IPlatformFile::FDirectoryVisitorFunc VisitorFunc = VisitorLambda;
	PlatformFile.IterateDirectoryRecursively(*JsonDir_READ, VisitorFunc);

	UE_LOG(
		LogJsonDataAsset,
		Log,
		TEXT("Added %i json data assets for json data root %s"),
		NumJsonDataAssetsAdded,
		*RootName.ToString());
}
#endif
