// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "JsonDataAsset.h"

// Global utility functions for json data asset system.
namespace OUU::Runtime::JsonData
{
	// If true, a separate package root is used for source files.
	OUUJSONDATARUNTIME_API bool ShouldUseSeparateSourceMountRoot();

	OUUJSONDATARUNTIME_API extern const FName GameRootName;

	OUUJSONDATARUNTIME_API FString GetSourceRoot_Full(const FName& RootName, EJsonDataAccessMode AccessMode);

#if WITH_EDITOR
	// Mount point for source files. Not required at runtime, but for some content browser functionality.
	// #TODO move into content browser source. This is specific to which mount is investigated!!!!
	OUUJSONDATARUNTIME_API FString GetSourceMountPointRoot_Package(const FName& RootName);
	OUUJSONDATARUNTIME_API FString GetSourceMountPointRoot_DiskFull(const FName& RootName);
#endif

	// Mount point for generated packages.
	// Save into Save dir, so the packages are not versioned and can safely be deleted on engine startup.
	OUUJSONDATARUNTIME_API FString GetCacheMountPointRoot_Package(const FName& RootName);
	OUUJSONDATARUNTIME_API FString GetCacheMountPointRoot_DiskFull(const FName& RootName);

	OUUJSONDATARUNTIME_API bool PackageIsJsonData(const FString& PackagePath);

	OUUJSONDATARUNTIME_API FString PackageToDataRelative(const FString& PackagePath);

	OUUJSONDATARUNTIME_API FString PackageToSourceFull(const FString& PackagePath, EJsonDataAccessMode AccessMode);

	// Take a path that is relative to the project root and convert it into a package path.
	OUUJSONDATARUNTIME_API FString SourceFullToPackage(const FString& FullPath, EJsonDataAccessMode AccessMode);

	OUUJSONDATARUNTIME_API FString PackageToObjectName(const FString& Package);

	OUUJSONDATARUNTIME_API bool ShouldIgnoreInvalidExtensions();

	OUUJSONDATARUNTIME_API bool ShouldReadFromCookedContent();
	OUUJSONDATARUNTIME_API bool ShouldWriteToCookedContent();
	OUUJSONDATARUNTIME_API void CheckJsonPaths();

	namespace Private
	{
		// not API exposed, because it's only for internal use
		void Delete(const FString& PackagePath);
	} // namespace Private
} // namespace OUU::Runtime::JsonData
