// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "JsonDataCustomVersions.generated.h"

class FJsonObject;
class FCustomVersionContainer;

/**
 * Store map of custom versions required for a json data asset file.
 */
USTRUCT(BlueprintType)
struct OUUJSONDATARUNTIME_API FJsonDataCustomVersions
{
	GENERATED_BODY()

public:
	FJsonDataCustomVersions() = default;
	FJsonDataCustomVersions(const TSet<FGuid>& CustomVersionGuids);

	int32 GetCustomVersion(const FGuid& CustomVersionGuid) const;

	/**
	 * Collect all custom versions used by the given struct for saving.
	 */
	void CollectVersions(UStruct* StructDefinition, const void* Data);

	TSharedPtr<FJsonObject> ToJsonObject() const;
	void ReadFromJsonObject(const TSharedPtr<FJsonObject>& JsonObject);

	FCustomVersionContainer ToCustomVersionContainer() const;

private:
	UPROPERTY(EditAnywhere)
	TMap<FGuid, int32> VersionsByGuid;
};
