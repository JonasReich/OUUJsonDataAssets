// Copyright (c) 2023 Jonas Reich & Contributors

#include "OUUJsonDataEditorSubsystem.h"

#include "JsonDataAsset.h"
#include "JsonDataAssetPathDetailsCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectIterator.h"

void UOUUJsonDataEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	auto& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	for (TObjectIterator<UStruct> It; It; ++It)
	{
		if (It->IsChildOf(FJsonDataAssetPath::StaticStruct()) || It->IsChildOf(FSoftJsonDataAssetPtr::StaticStruct())
			|| It->IsChildOf(FJsonDataAssetPtr::StaticStruct()))
		{
			PropertyEditor.RegisterCustomPropertyTypeLayout(
				It->GetFName(),
				FOnGetPropertyTypeCustomizationInstance::CreateLambda([]() -> TSharedRef<IPropertyTypeCustomization> {
					return MakeShared<FJsonDataAssetPathCustomization>();
				}));
		}
	}
}

void UOUUJsonDataEditorSubsystem::Deinitialize()
{
	if (auto pPropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		for (TObjectIterator<UStruct> It; It; ++It)
		{
			if (It->IsChildOf(FJsonDataAssetPath::StaticStruct())
				|| It->IsChildOf(FSoftJsonDataAssetPtr::StaticStruct())
				|| It->IsChildOf(FJsonDataAssetPtr::StaticStruct()))
			{
				pPropertyEditor->UnregisterCustomPropertyTypeLayout(It->GetFName());
			}
		}
	}
}
