// Copyright (c) 2023 Jonas Reich & Contributors

#include "OUUJsonDataRuntimeVersion.h"

#include "UObject/DevObjectVersion.h"

// Unique OUUJsonDataRuntime version id
const FGuid FOUUJsonDataRuntimeVersion::GUID("0E26539A-1A69-4EAE-81CE-70D356B69D52");

// Register OUUJsonDataRuntime custom version with core
FDevVersionRegistration GRegisterOUUJsonDataRuntimeVersion(
	FOUUJsonDataRuntimeVersion::GUID,
	FOUUJsonDataRuntimeVersion::LatestVersion,
	TEXT("OUUJsonDataRuntime"));
