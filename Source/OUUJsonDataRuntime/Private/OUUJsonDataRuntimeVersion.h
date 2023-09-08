// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "UObject/NoExportTypes.h"

// Custom serialization version for changes made in the OUURuntime module.
struct FOUUJsonDataRuntimeVersion
{
public:
	enum Type
	{
		// When custom version was first introduced
		InitialVersion = 0,

		// This change introduced custom FArchive serialization for json asset paths and smart pointers.
		// This change does not affect text serialization, so it's not needed for
		// UJsonDataAsset::GetRelevantCustomVersions()
		AddedJsonDataAssetPathSerialization = InitialVersion,

		// This change introduced cache invalidation based on time stamps and FOUUJsonDataRuntimeVersion.
		// Any future version introduced here will invalidate the json data cache and lead to a full cache refresh.
		TimeAndVersionCacheInvalidation,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FOUUJsonDataRuntimeVersion() = delete;
};
