// Copyright (c) 2023 Jonas Reich & Contributors

#include "JsonDataAsset.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine.h"
#include "HAL/PlatformFile.h"
#include "JsonDataAssetGlobals.h"
#include "JsonDataAssetSubsystem.h"
#include "JsonLibrary.h"
#include "JsonObjectConverter.h"
#include "LogJsonDataAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/SavePackage.h"

#if WITH_EDITOR
	#include "Editor.h"
	#include "FileHelpers.h"
	#include "SourceControlHelpers.h"
	#include "AssetViewUtils.h"
	#include "AssetToolsModule.h"
	#include "Misc/DataValidation.h"
#endif

namespace OUU::JsonData::Runtime::Private
{
	void Delete(const FString& PackagePath)
	{
		auto FullPath = OUU::JsonData::Runtime::PackageToSourceFull(PackagePath, EJsonDataAccessMode::Write);
		if (OUU::JsonData::Runtime::ShouldWriteToCookedContent())
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.DeleteFile(*FullPath);
		}
		else
		{
#if WITH_EDITOR
			USourceControlHelpers::MarkFileForDelete(FullPath);
#else
			UE_LOG(
				LogJsonDataAsset,
				Warning,
				TEXT("Can't delete file '%s' from uncooked content in non-editor context."),
				*FullPath);
#endif
		}
	}
} // namespace OUU::JsonData::Runtime::Private

//---------------------------------------------------------------------------------------------------------------------

bool UJsonDataAsset::IsInJsonDataContentRoot() const
{
	return OUU::JsonData::Runtime::PackageIsJsonData(GetPackage()->GetPathName());
}

bool UJsonDataAsset::IsFileBasedJsonAsset() const
{
	return IsAsset();
}

bool UJsonDataAsset::ImportJson(TSharedPtr<FJsonObject> JsonObject, bool bCheckClassMatches)
{
	// ---
	// Header information
	// ---
	if (bCheckClassMatches)
	{
		FString ClassName = JsonObject->GetStringField(TEXT("Class"));
		// Better search for the class instead of mandating a perfect string match
		auto* JsonClass = Cast<UClass>(FSoftObjectPath(ClassName).ResolveObject());

		// There is a chance in the editor that a blueprint class may get recompiled while a json object is being
		// loaded. In that case	the IsChildOf check will fail and we need to manually check if our current class is the
		// old version of the correct class.
		const bool bIsReinstantiatedBlueprint = GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists) && JsonClass
			&& GetClass()->GetName().Contains(JsonClass->GetName());

		if (GetClass()->IsChildOf(JsonClass) == false && bIsReinstantiatedBlueprint == false)
		{
			UE_JSON_DATA_MESSAGELOG(
				Error,
				this,
				TEXT("Class name in json object (%s) does not match class of object (%s)"),
				*ClassName,
				*GetClass()->GetName());
			return false;
		}
	}

	FEngineVersion EngineVersion;
	if (JsonObject->HasField(TEXT("EngineVersion")))
	{
		const FString JsonVersionString = JsonObject->GetStringField(TEXT("EngineVersion"));
		const bool bIsLicenseeVersion = JsonObject->GetBoolField(TEXT("IsLicenseeVersion"));
		if (!FEngineVersion::Parse(JsonVersionString, OUT EngineVersion))
		{
			UE_JSON_DATA_MESSAGELOG(Error, this, TEXT("Json file has an invalid 'EngineVersion' field value"));
			return false;
		}

		uint32 Changelist = EngineVersion.GetChangelist();
		EngineVersion.Set(
			EngineVersion.GetMajor(),
			EngineVersion.GetMinor(),
			EngineVersion.GetPatch(),
			Changelist | (bIsLicenseeVersion ? (1U << 31) : 0),
			EngineVersion.GetBranch());

		if (!FEngineVersion::Current().IsCompatibleWith(EngineVersion))
		{
			UE_JSON_DATA_MESSAGELOG(
				Error,
				this,
				TEXT("Json file for"
					 "has an invalid engine version: %s"
					 "is not compatible with %s"
					 ". Last compatible version: %s"),
				*JsonVersionString,
				*FEngineVersion::Current().ToString(),
				*FEngineVersion::CompatibleWith().ToString());
			return false;
		}
	}

	FJsonDataCustomVersions CustomVersions;
	const TSharedPtr<FJsonObject>* ppCustomVersionsObject = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("CustomVersions"), ppCustomVersionsObject))
	{
		CustomVersions.ReadFromJsonObject(*ppCustomVersionsObject);
	}

	CustomVersions.EnsureExpectedVersions(GetRelevantCustomVersions());

	// ---
	// Property data
	// ---

	auto Data = JsonObject->GetObjectField(TEXT("Data"));
	if (!Data.IsValid())
	{
		UE_JSON_DATA_MESSAGELOG(Error, this, TEXT("Json file does not contain a 'Data' field"));
		return false;
	}

	// Reset object properties to class defaults
	{
		auto* CDO = GetClass()->GetDefaultObject();
		UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
		UEngine::CopyPropertiesForUnrelatedObjects(CDO, this, Options);
	}

	if (!UOUUJsonLibrary::JsonObjectToUStruct(Data.ToSharedRef(), GetClass(), this, 0, 0))
	{
		UE_JSON_DATA_MESSAGELOG(Error, this, TEXT("Failed to import json 'Data' field into UObject properties"));
		return false;
	}

	return PostLoadJsonData(EngineVersion, CustomVersions, Data.ToSharedRef());
}

TSharedRef<FJsonObject> UJsonDataAsset::ExportJson() const
{
	auto Result = MakeShared<FJsonObject>();

	// Header information
	{
		Result->SetStringField(TEXT("Class"), GetClass()->GetPathName());
		Result->SetStringField(TEXT("EngineVersion"), FEngineVersion::Current().ToString());
		Result->SetBoolField(TEXT("IsLicenseeVersion"), FEngineVersion::Current().IsLicenseeVersion());

		const FJsonDataCustomVersions CustomVersions(GetRelevantCustomVersions());
		Result->SetObjectField(TEXT("CustomVersions"), CustomVersions.ToJsonObject());
	}

	// Property data
	{
		FOUUJsonLibraryObjectFilter Filter;
		Filter.SubObjectDepthLimit = 0;

		// No requirements. We had Edit here before which prevented hidden properties that aren't eitable in UI
		const int64 CheckFlags = 0;
		const int64 SkipFlags = CPF_Transient;

		// Data going into the cooked content directory should write all properties into the files to have a baseline
		// for modders. Data going into the regular editor saves should perform delta serialization to support
		// propagation of values from base class defaults.
		bool bOnlyModifiedProperties = OUU::JsonData::Runtime::ShouldWriteToCookedContent() == false;

		Result->SetObjectField(
			TEXT("Data"),
			UOUUJsonLibrary::UObjectToJsonObject(this, Filter, CheckFlags, SkipFlags, bOnlyModifiedProperties));
	}

	return Result;
}

FString UJsonDataAsset::GetJsonFilePathAbs(EJsonDataAccessMode AccessMode) const
{
	return OUU::JsonData::Runtime::PackageToSourceFull(GetPackage()->GetPathName(), AccessMode);
}

FJsonDataAssetPath UJsonDataAsset::GetPath() const
{
	return FJsonDataAssetPath::FromPackagePath(GetPackage()->GetPathName());
}

bool UJsonDataAsset::ImportJsonFile()
{
	if (IsFileBasedJsonAsset() == false)
	{
		UE_JSON_DATA_MESSAGELOG(

			Error,
			this,
			TEXT("does not have an associated json file to import from. Did you try to call ImportJsonFile on a CDO?"));
		return false;
	}

	auto ThisIfSuccess = LoadJsonDataAsset_Internal(GetPath(), this);
	if (IsValid(ThisIfSuccess))
	{
		ensureMsgf(
			this == ThisIfSuccess,
			TEXT("Importing json file was successful, but returned a different object. Should always return this or "
				 "nullptr."));
		return true;
	}
	return false;
}

bool UJsonDataAsset::ExportJsonFile() const
{
	if (IsFileBasedJsonAsset() == false)
	{
		UE_JSON_DATA_MESSAGELOG(

			Error,
			this,
			TEXT("does not have an associated json file to export to. Did you try to call ExportJsonFile on a CDO?"));
		return false;
	}

	if (!IsInJsonDataContentRoot())
	{
		UE_JSON_DATA_MESSAGELOG(

			Error,
			this,
			TEXT("is a json data asset, but the generated asset is not located in /JsonData/ content directory. Failed "
				 "to export json file."));
		return false;
	}

	const FString SavePath = GetJsonFilePathAbs(EJsonDataAccessMode::Write);

	TSharedRef<FJsonObject> JsonObject = ExportJson();
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(OUT & JsonString);
	if (!FJsonSerializer::Serialize(JsonObject, JsonWriter))
	{
		UE_JSON_DATA_MESSAGELOG(Error, this, TEXT("Failed to serialize json for object properties"));
		return false;
	}

#if WITH_EDITOR
	if (OUU::JsonData::Runtime::ShouldWriteToCookedContent() == false)
	{
		USourceControlHelpers::CheckOutFile(SavePath, true);
	}
#endif

	if (!FFileHelper::SaveStringToFile(JsonString, *SavePath))
	{
		UE_JSON_DATA_MESSAGELOG(Error, this, TEXT("Failed to save json string to file %s"), *SavePath);
		return false;
	}
	UE_LOG(LogJsonDataAsset, Log, TEXT("ExportJsonFile - Saved %s"), *SavePath);

#if WITH_EDITOR
	if (OUU::JsonData::Runtime::ShouldWriteToCookedContent() == false)
	{
		if (USourceControlHelpers::CheckOutOrAddFile(SavePath) == false)
		{
			UE_JSON_DATA_MESSAGELOG(Error, this, TEXT("failed to check out or add file"));
		}
	}
#endif
	return true;
}

bool UJsonDataAsset::PostLoadJsonData(
	const FEngineVersion& EngineVersion,
	const FJsonDataCustomVersions& CustomVersions,
	TSharedRef<FJsonObject> JsonObject)
{
	return true;
}

bool UJsonDataAsset::MustHandleRename(UObject* OldOuter, const FName OldName) const
{
	if (IsFileBasedJsonAsset() == false)
	{
		// Never need to handle renames of non-file json assets
		return false;
	}
	const auto NewOuter = GetOuter();
	if (NewOuter == OldOuter)
	{
		// From our observation, every "real rename" is accompanied by a change in outer
		return false;
	}

	return (OldOuter == nullptr || NewOuter == nullptr || OldOuter->GetPathName() != NewOuter->GetPathName());
}

TSet<FGuid> UJsonDataAsset::GetRelevantCustomVersions() const
{
	return {};
}

UJsonDataAsset* UJsonDataAsset::LoadJsonDataAsset_Internal(FJsonDataAssetPath Path, UJsonDataAsset* ExistingDataAsset)
{
	if (Path.IsNull())
	{
		return nullptr;
	}

	const FString InPackagePath = Path.GetPackagePath();
	const FString LoadPath = OUU::JsonData::Runtime::PackageToSourceFull(InPackagePath, EJsonDataAccessMode::Read);

	if (!FPaths::FileExists(LoadPath))
	{
		UE_JSON_DATA_MESSAGELOG(Warning, nullptr, TEXT("File %s does not exist"), *LoadPath);
		return nullptr;
	}

	if (!LoadPath.EndsWith(TEXT(".json")))
	{
		UE_JSON_DATA_MESSAGELOG(Warning, nullptr, TEXT("Path %s does not end in '.json'"), *LoadPath);
		return nullptr;
	}

	FString JsonString;
	if (FFileHelper::LoadFileToString(JsonString, *LoadPath))
	{
		UE_LOG(LogJsonDataAsset, Verbose, TEXT("Loaded %s"), *LoadPath);
	}
	else
	{
		UE_JSON_DATA_MESSAGELOG(Error, nullptr, TEXT("Failed to load %s"), *LoadPath);
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("LoadJsonDataAsset - Unable to parse json=[%s]"), *JsonString);
		UE_JSON_DATA_MESSAGELOG(
			Error,
			nullptr,
			TEXT("Failed to parse json for %s. See output log above for more information"),
			*LoadPath);
		return nullptr;
	}

	const FString ObjectName = OUU::JsonData::Runtime::PackageToObjectName(InPackagePath);
	FString PackageFilename;

	// Even if existing asset was not passed in, it still might be on disk.
	if (IsValid(ExistingDataAsset) == false && FPackageName::DoesPackageExist(InPackagePath, &PackageFilename))
	{
		TSoftObjectPtr<UJsonDataAsset> ExistingAssetPath(InPackagePath + TEXT(".") + ObjectName);
		ExistingDataAsset = ExistingAssetPath.LoadSynchronous();
	}

	UJsonDataAsset* ExistingOrGeneratedAsset = nullptr;
	bool bCheckClassMatches = true;
	if (IsValid(ExistingDataAsset))
	{
		ExistingOrGeneratedAsset = ExistingDataAsset;
		bCheckClassMatches = true;
	}
	else
	{
		FString ClassName = JsonObject->GetStringField(TEXT("Class"));
		// Need to use TryLoad() instead of ResolveObject() so blueprint classes can be loaded.
		auto* pClass = Cast<UClass>(FSoftObjectPath(ClassName).TryLoad());
		if (!pClass)
		{
			UE_JSON_DATA_MESSAGELOG(
				Error,
				nullptr,
				TEXT("Json file %s does not have a class field or invalid class name (%s)"),
				*LoadPath,
				*ClassName);
			return nullptr;
		}

		if (!pClass->IsChildOf<UJsonDataAsset>())
		{
			UE_JSON_DATA_MESSAGELOG(
				Error,
				nullptr,
				TEXT("Class %s is not a child of %s - encountered while loading %s"),
				*GetNameSafe(pClass),
				*UJsonDataAsset::StaticClass()->GetName(),
				*LoadPath);
			return nullptr;
		}

		UPackage* GeneratedPackage = CreatePackage(*InPackagePath);
		ExistingOrGeneratedAsset =
			NewObject<UJsonDataAsset>(GeneratedPackage, pClass, *ObjectName, RF_Public | RF_Standalone);
		// No need to check the class. We already did
		bCheckClassMatches = false;
	}

	checkf(IsValid(ExistingOrGeneratedAsset), TEXT("The json asset is expected to be valid at this point"));
	if (!ExistingOrGeneratedAsset->ImportJson(JsonObject, bCheckClassMatches))
	{
		return nullptr;
	}

	if (!IsValid(ExistingDataAsset))
	{
		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(ExistingOrGeneratedAsset);
	}

	return ExistingOrGeneratedAsset;
}

bool UJsonDataAsset::Rename(
	const TCHAR* NewName /*= nullptr*/,
	UObject* NewOuter /*= nullptr*/,
	ERenameFlags Flags /*= REN_None*/)
{
	if (IsFileBasedJsonAsset() == false)
	{
		return Super::Rename(NewName, NewOuter, Flags);
	}

#if WITH_EDITOR
	if (!UJsonDataAssetSubsystem::AutoExportJsonEnabled())
	{
		UE_JSON_DATA_MESSAGELOG(Error, this, TEXT("Can't rename asset while auto export to json is disabled."));
		return false;
	}

	return Super::Rename(NewName, NewOuter, Flags);
#else
	// Do not allow renaming outside of the editor
	return false;
#endif
}

void UJsonDataAsset::PostRename(UObject* OldOuter, const FName OldName)
{
#if WITH_EDITOR
	Super::PostRename(OldOuter, OldName);

	// We only need to remove the old json file if our outer (the package) or its path has changed. Otherwise the file
	// can stay where it is. When the package is renamed in the editor, we are (at least from my testing) always
	// assigned a new outer.
	if (MustHandleRename(OldOuter, OldName) == false)
	{
		return;
	}

	auto OldPackagePathName = OldOuter->GetPathName();
	OUU::JsonData::Runtime::Private::Delete(OldPackagePathName);

	{
		if (UObjectRedirector* Redirector = FindObjectFast<UObjectRedirector>(OldOuter, OldName))
		{
			FScopedSlowTask SlowTask(1, INVTEXT("Fixing up redirectors"));
			SlowTask.MakeDialog();
			SlowTask.EnterProgressFrame(1, INVTEXT("Fixing up referencers..."));
			// Load the asset tools module
			FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();

			// Ideally, we wouldn't want to leave a choice for this, because we can't allow keeping around redirectors.
			// If we allow a choice it should be before the rename starts in the first place.
			bool bCheckoutAndPrompt = false;
			AssetToolsModule.Get()
				.FixupReferencers({Redirector}, bCheckoutAndPrompt, ERedirectFixupMode::DeleteFixedUpRedirectors);

			// Not prompting sometimes leads to json assets referncing other json assets being ignored
			if (UObjectRedirector* RedirectorStillThere = FindObjectFast<UObjectRedirector>(OldOuter, OldName))
			{
				if (IsValid(RedirectorStillThere))
				{
					UE_JSON_DATA_MESSAGELOG(
						Warning,
						Redirector,
						TEXT("is a redirector to a json data asset, which can't be checked-in."));

					FMessageLog(JSON_DATA_MESSAGELOG_CATEGORY)
						.Notify(INVTEXT("Data loss imminent if not immediately resolved!"));
				}
			}
		}
		else
		{
			// not every rename creates redirectors, so this case is ok + expected
		}
	}
#else
	checkf(false, TEXT("Renaming/moving is not allowed outside of the editor, so this should never be called."));
#endif
}

void UJsonDataAsset::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);
#if WITH_EDITOR
	if (IsFileBasedJsonAsset() && UJsonDataAssetSubsystem::AutoExportJsonEnabled())
	{
		// Only export the json files if the subsystem is fully initialized.
		// Otherwise we resave the newly loaded uassets created from json back to json.
		// Also, during editor startup the source control provider is not fully initialized and we run into other
		// issues.
		ExportJsonFile();
	}
#endif
}

void UJsonDataAsset::PostLoad()
{
	Super::PostLoad();

	// Not called for newly created objects, so we should not have to manually prevent duplicate importing.
	if (bIsInPostLoad == false && IsFileBasedJsonAsset())
	{
		bIsInPostLoad = true;
		ImportJsonFile();
		bIsInPostLoad = false;
	}
}

void UJsonDataAsset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (IsFileBasedJsonAsset())
	{
		ExportJsonFile();
	}
}

bool UJsonDataAsset::IsFullNameStableForNetworking() const
{
	return false;
}

bool UJsonDataAsset::IsSupportedForNetworking() const
{
	return false;
}

#if WITH_EDITOR
EDataValidationResult UJsonDataAsset::IsDataValid(class FDataValidationContext& Context)
{
	auto Result = Super::IsDataValid(Context);
	if (!IsInJsonDataContentRoot())
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("%s is a json data asset, but not located in /JsonData/ content directory. This will prevent correct "
				 "json data loading!"),
			*GetNameSafe(this))));
		return EDataValidationResult::Invalid;
	}

	// Check if there are any hard package refs to either the package or object.
	// Both are NEVER permitted, as we only allow referencing via json data asset path, which produces a soft object
	// reference.
	{
		TArray<FAssetIdentifier> PackageReferencers;
		IAssetRegistry::Get()->GetReferencers(
			FAssetIdentifier(GetOutermost()->GetFName()),
			OUT PackageReferencers,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::EDependencyQuery::Hard);
		for (auto& Referencer : PackageReferencers)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("%s has hard reference to PACKAGE %s"),
				*Referencer.ToString(),
				*GetOutermost()->GetName())));
		}

		TArray<FAssetIdentifier> ObjectReferencers;
		IAssetRegistry::Get()->GetReferencers(
			FAssetIdentifier(GetFName()),
			OUT ObjectReferencers,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::EDependencyQuery::Hard);
		for (auto& Referencer : ObjectReferencers)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("%s has hard reference to OBJECT %s"),
				*Referencer.ToString(),
				*GetOutermost()->GetName())));
		}
	}

	return Result;
}
#endif
