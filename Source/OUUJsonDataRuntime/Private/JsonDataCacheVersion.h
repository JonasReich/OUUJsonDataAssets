// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "Dom/JsonObject.h"
#include "JsonDataAssetGlobals.h"
#include "LogJsonDataAsset.h"
#include "OUUJsonDataRuntimeVersion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

namespace OUU::JsonData::Runtime
{
	namespace JsonProps
	{
		static const FString EngineVersion = TEXT("EngineVersion");
		static const FString IsLicenseeVersion = TEXT("IsLicenseeVersion");
		static const FString JsonRuntimeVersion = TEXT("JsonRuntimeVersion");
	} // namespace JsonProps

	/** Version marker used to detect if the cache is valid / compatible with the current version. */
	struct FCacheVersion
	{
		bool bIsValid = false;
		FEngineVersion EngineVersion;
		bool bEngineIsLicenseeVersion;
		int32 JsonRuntimeVersion = INDEX_NONE;

		static FString GetPathAbs()
		{
			return FPaths::Combine(OUU::JsonData::Runtime::GetCacheDir_DiskFull(), "CacheVersion.json");
		}

		static FCacheVersion Current()
		{
			FCacheVersion Result;
			Result.bIsValid = true;
			Result.EngineVersion = FEngineVersion::Current();
			Result.bEngineIsLicenseeVersion = FEngineVersion::Current().IsLicenseeVersion();
			Result.JsonRuntimeVersion = static_cast<int32>(FOUUJsonDataRuntimeVersion::LatestVersion);
			return Result;
		}

		void Write()
		{
			auto JsonObject = MakeShared<FJsonObject>();
			JsonObject->SetStringField(JsonProps::EngineVersion, EngineVersion.ToString());
			JsonObject->SetBoolField(JsonProps::IsLicenseeVersion, bEngineIsLicenseeVersion);
			JsonObject->SetNumberField(JsonProps::JsonRuntimeVersion, JsonRuntimeVersion);

			FString JsonString;
			TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(OUT & JsonString);
			ensure(FJsonSerializer::Serialize(JsonObject, JsonWriter));
			ensure(FFileHelper::SaveStringToFile(JsonString, *GetPathAbs()));
		}

		static FCacheVersion Read()
		{
			FCacheVersion Result;
			const FString FilePath = GetPathAbs();

			// is this sufficient?
			Result.bIsValid = FPaths::FileExists(*FilePath);

			if (Result.bIsValid == false)
				return Result;

			FString JsonString;
			ensure(FFileHelper::LoadFileToString(JsonString, *FilePath));
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
			ensure(FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid());

			ensure(
				FEngineVersion::Parse(JsonObject->GetStringField(JsonProps::EngineVersion), OUT Result.EngineVersion));
			Result.bEngineIsLicenseeVersion = JsonObject->GetBoolField(JsonProps::IsLicenseeVersion);
			uint32 Changelist = Result.EngineVersion.GetChangelist();
			Result.EngineVersion.Set(
				Result.EngineVersion.GetMajor(),
				Result.EngineVersion.GetMinor(),
				Result.EngineVersion.GetPatch(),
				Changelist | (Result.bEngineIsLicenseeVersion ? (1U << 31) : 0),
				Result.EngineVersion.GetBranch());

			ensure(JsonObject->TryGetNumberField(JsonProps::JsonRuntimeVersion, OUT Result.JsonRuntimeVersion));

			return Result;
		}

		static bool IsCacheCompatible(const FCacheVersion& New, const FCacheVersion& Old)
		{
			// Cache is never considered compatible if either version is invalid
			if ((Old.bIsValid && New.bIsValid) == false)
				return false;

			if (New.EngineVersion.IsCompatibleWith(Old.EngineVersion) == false)
				return false;

			// Consider any mismatch of the json runtime version as a reason to invalidate the cache
			if (New.JsonRuntimeVersion != Old.JsonRuntimeVersion)
				return false;

			return true;
		}

		// If false, the cache is stale and needs to be invalidated in its entirety.
		static bool IsCacheCompatible()
		{
			auto CacheVersion = Read();
			auto CurrentVersion = Current();
			return IsCacheCompatible(CurrentVersion, CacheVersion);
		}
	};
} // namespace OUU::JsonData::Runtime
