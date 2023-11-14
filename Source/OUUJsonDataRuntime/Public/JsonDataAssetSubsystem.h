// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "Engine.h"
#include "JsonDataAsset.h"
#include "JsonDataAssetPath.h"
#include "Subsystems/EngineSubsystem.h"

#include "JsonDataAssetSubsystem.generated.h"

USTRUCT()
struct FJsonDataAssetPaths
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FJsonDataAssetPath> Paths;
};

USTRUCT()
struct FJsonDataAssetMetaDataCache
{
	GENERATED_BODY()

public:
	bool SaveToFile(const FString& FilePath) const;
	bool LoadFromFile(const FString& FilePath);

public:
	UPROPERTY()
	TMap<FTopLevelAssetPath, FJsonDataAssetPaths> PathsByClass;
};

UCLASS(BlueprintType)
class OUUJSONDATARUNTIME_API UJsonDataAssetSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	FORCEINLINE static UJsonDataAssetSubsystem& Get()
	{
		return *GEngine->GetEngineSubsystem<UJsonDataAssetSubsystem>();
	}

	// - USubsystem
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;
	// --

	// If true, asset saves/moves will export to json
	static bool AutoExportJsonEnabled();

	/**
	 * Serialize the given path over the network.
	 */
	static void NetSerializePath(FJsonDataAssetPath& Path, FArchive& Ar);

	/**
	 * Import all .json into UJsonDataAssets.
	 * This does not delete stale UJsonDataAssets that don't have a matching .json file anymore.
	 * It does reload all property data of existing json assets, unless bOnlyMissing is true.
	 */
	UFUNCTION()
	void ImportAllAssets(bool bOnlyMissing);

	/**
	 * Rescan all json data asset files on disk.
	 */
	void RescanAllAssets();

	// Get all json data assets of the given class.
	UFUNCTION(BlueprintCallable)
	TArray<FJsonDataAssetPath> GetJsonAssetsByClass(
		TSoftClassPtr<UJsonDataAsset> Class,
		const bool bSearchSubClasses = false) const;

private:
	void ImportAllAssets(const FName& RootName, bool bOnlyMissing);

public:
	/**
	 * Add a new root for plugin json data files.
	 *
	 * This maps a source folder
	 *		<PluginRoot>/<ouu.JsonData.SourceUncooked>/
	 * to a content root
	 *		/JsonData/Plugins/<PluginName>/
	 * Data files are copied to
	 *		<GameRoot>/<ouu.JsonData.SourceCooked>/Plugins/<PluginName>/
	 * during cook.
	 *
	 * This feature is the reason why it's disallowed to create a Plugins/ folder inside the directory for game data
	 * files.
	 */
	UFUNCTION(BlueprintCallable)
	void AddPluginDataRoot(const FName& PluginName);

	const TMap<FName, FString>& GetSourceMappings(EJsonDataAccessMode AccessMode) const;
	const TMap<FName, FString>& GetSourceMappings(bool bUseCookedContent) const;
	const TArray<FString>& GetAllSourceDirectories(EJsonDataAccessMode AccessMode) const;
	const TArray<FName>& GetAllPluginRootNames() const { return AllPluginRootNames; }
	FString GetVirtualRoot(const FName& RootName) const;

	/** @retuns NAME_None if the path does not start with a registered virtual root. */
	FName GetRootNameForPackagePath(const FString& PackagePath) const;

	/**
	 * Works both for cooked and uncooked source paths.
	 * Paths need to be normalized (forward facing directory slashes)
	 * @retuns NAME_None if the path does not start with a registered virtual root.
	 */
	FName GetRootNameForSourcePath(const FString& PackagePath) const;

	bool IsPathInSourceDirectoryOfNamedRoot(const FString& SourcePath, const FName& RootName) const;

public:
	// Called whenever a new plugin root is added.
	// Required for the content browser extension to be able to react to late plugin registrations.
	DECLARE_EVENT_OneParam(UJsonDataAssetSubsystem, FOnNewPluginRootAdded, const FName&);
	FOnNewPluginRootAdded OnNewPluginRootAdded;

private:
	FString GetMetaDataCacheFilePath(const EJsonDataAccessMode AccessMode) const;

	void RegisterMountPoints(const FName& RootName);
	void UnregisterMountPoints(const FName& RootName);

	void PostEngineInit();

#if WITH_EDITOR
	void HandlePreBeginPIE(const bool bIsSimulating);

	void CleanupAssetCache(const FName& RootName);

	UFUNCTION()
	void HandlePackageDeleted(UPackage* Package);

	UFUNCTION()
	void ModifyCook(TArray<FString>& OutExtraPackagesToCook);
	void ModifyCookInternal(
		const FName& RootName,
		TSet<FName>& OutDependencyPackages,
		FJsonDataAssetMetaDataCache& OutMetaDataCache);
#endif

	bool bIsInitialAssetImportCompleted = false;
	bool bAutoExportJson = false;
	bool bJsonDataAssetListBuilt = false;

	// Maps from plugin mount points (like /JsonData/Plugins/OpenUnrealUtilities/) to source disk paths (like
	// <ProjectRoot>/Plugins/OpenUnrealUtilities/Data/)
	TMap<FName, FString> SourceDirectories_Uncooked;
	TMap<FName, FString> SourceDirectories_Cooked;

	// Quick list to look up all source directories.
	TArray<FString> AllSourceDirectories_Uncooked;
	TArray<FString> AllSourceDirectories_Cooked;

	// Mapping of all json data asset files, used for fast net serialization
	TArray<FName> AllJsonDataAssetsByIndex;
	TMap<FName, int32> AllJsonDataAssetsByPath;
	// Actual number of bits needed to fully serialize an index into AllJsonDataAssetsByIndex. Will be set to actual
	// number of bits needed in UJsonDataAssetSubsystem::RescanAllAssets.
	int64 PathIndexNetSerializeBits = 31;

	TArray<FName> AllPluginRootNames;
	TArray<FName> AllRootNames;

	FJsonDataAssetMetaDataCache AssetMetaDataCache;
};