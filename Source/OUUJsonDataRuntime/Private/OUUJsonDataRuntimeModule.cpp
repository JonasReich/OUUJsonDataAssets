// Copyright (c) 2023 Jonas Reich & Contributors

#include "CoreMinimal.h"

#include "JsonDataAssetSubsystem.h"
#include "LogJsonDataAsset.h"
#include "Modules/ModuleManager.h"

class FOUUJsonDataRuntimeModule : public IModuleInterface
{
	// - IModuleInterface
	void StartupModule() override
	{
		FCoreDelegates::OnPostEngineInit.AddLambda(
			[]() { UJsonDataAssetSubsystem::Get().AddPluginDataRoot(TEXT("OUUJsonDataAssets")); });
	}
};

IMPLEMENT_MODULE(FOUUJsonDataRuntimeModule, OUUJsonDataRuntime)

DEFINE_LOG_CATEGORY(LogJsonDataAsset)
