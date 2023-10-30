// Copyright (c) 2023 Jonas Reich & Contributors

#include "JsonDataAssetPathDetailsCustomization.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserDataDragDropOp.h"
#include "DetailWidgetRow.h"
#include "IContentBrowserSingleton.h"
#include "Input/DragAndDrop.h"
#include "JsonAssetReferenceFilter.h"
#include "JsonDataAsset.h"
#include "JsonDataAssetEditor.h"
#include "JsonDataAssetGlobals.h"
#include "LogJsonDataAsset.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "SDropTarget.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"

namespace OUU::JsonData::Editor::Private
{
	TOptional<FJsonDataAssetPath> GetFirstJsonPathFromDragDropOp(
		const TSharedPtr<FContentBrowserDataDragDropOp>& ContentDragDropOp)
	{
		auto Files = ContentDragDropOp->GetDraggedFiles();
		const auto Root = OUU::JsonData::Runtime::GetSourceMountPointRoot_Package(OUU::JsonData::Runtime::GameRootName);
		const auto Prefix = FString::Printf(TEXT("/All%s"), *Root);
		for (auto File : Files)
		{
			if (auto* FileItem = File.GetPrimaryInternalItem())
			{
				auto VirtualPath = FileItem->GetVirtualPath().ToString();
				if (VirtualPath.StartsWith(Prefix))
				{
					VirtualPath.RemoveFromStart(TEXT("/All"));
					FJsonDataAssetPath Result =
						OUU::Editor::JsonData::ConvertMountedSourceFilenameToDataAssetPath(VirtualPath);
					return Result;
				}
			}
		}
		return {};
	}

	UClass* GetObjectPropertyClass(const FProperty* Property)
	{
		UClass* Class = nullptr;

		if (CastField<const FObjectPropertyBase>(Property) != nullptr)
		{
			Class = CastField<const FObjectPropertyBase>(Property)->PropertyClass;
			if (Class == nullptr)
			{
				UE_LOG(
					LogJsonDataAsset,
					Warning,
					TEXT("Object Property (%s) has a null class, falling back to UObject"),
					*Property->GetFullName());
				Class = UObject::StaticClass();
			}
		}
		else if (CastField<const FInterfaceProperty>(Property) != nullptr)
		{
			Class = CastField<const FInterfaceProperty>(Property)->InterfaceClass;
			if (Class == nullptr)
			{
				UE_LOG(
					LogJsonDataAsset,
					Warning,
					TEXT("Interface Property (%s) has a null class, falling back to UObject"),
					*Property->GetFullName());
				Class = UObject::StaticClass();
			}
		}
		else
		{
			ensureMsgf(
				Class != nullptr,
				TEXT("Property (%s) is not an object or interface class"),
				Property ? *Property->GetFullName() : TEXT("null"));
			Class = UObject::StaticClass();
		}
		return Class;
	}

	void GetClassFiltersFromPropertyMetadata(
		const FProperty* Property,
		const FProperty* MetadataProperty,
		TArray<const UClass*>& OutAllowedClassFilters,
		TArray<const UClass*>& OutDisallowedClassFilters)
	{
		const auto* ObjectClass = GetObjectPropertyClass(Property);

		// Copied from void SPropertyEditorAsset::InitializeClassFilters(const FProperty* Property)
		if (Property == nullptr)
		{
			OutAllowedClassFilters.Add(ObjectClass);
			return;
		}

		auto FindClass = [](const FString& InClassName) {
			UClass* Class = UClass::TryFindTypeSlow<UClass>(InClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
			if (!Class)
			{
				Class = LoadObject<UClass>(nullptr, *InClassName);
			}
			return Class;
		};

		const FString AllowedClassesFilterString = MetadataProperty->GetMetaData(TEXT("AllowedClasses"));
		if (!AllowedClassesFilterString.IsEmpty())
		{
			TArray<FString> AllowedClassFilterNames;
			AllowedClassesFilterString.ParseIntoArrayWS(AllowedClassFilterNames, TEXT(","), true);

			for (const FString& ClassName : AllowedClassFilterNames)
			{
				if (const UClass* Class = FindClass(ClassName))
				{
					// If the class is an interface, expand it to be all classes in memory that implement the class.
					if (Class->HasAnyClassFlags(CLASS_Interface))
					{
						for (const UClass* ClassWithInterface : TObjectRange<UClass>())
						{
							if (ClassWithInterface->ImplementsInterface(Class))
							{
								OutAllowedClassFilters.Add(ClassWithInterface);
							}
						}
					}
					else
					{
						OutAllowedClassFilters.Add(Class);
					}
				}
			}
		}

		if (OutAllowedClassFilters.Num() == 0)
		{
			// always add the object class to the filters if it was not further filtered
			OutAllowedClassFilters.Add(ObjectClass);
		}

		const FString DisallowedClassesFilterString = MetadataProperty->GetMetaData(TEXT("DisallowedClasses"));
		if (!DisallowedClassesFilterString.IsEmpty())
		{
			TArray<FString> DisallowedClassFilterNames;
			DisallowedClassesFilterString.ParseIntoArrayWS(DisallowedClassFilterNames, TEXT(","), true);

			for (const FString& ClassName : DisallowedClassFilterNames)
			{
				if (const UClass* Class = FindClass(ClassName))
				{
					// If the class is an interface, expand it to be all classes in memory that implement the class.
					if (Class->HasAnyClassFlags(CLASS_Interface))
					{
						for (const UClass* ClassWithInterface : TObjectRange<UClass>())
						{
							if (ClassWithInterface->ImplementsInterface(Class))
							{
								OutDisallowedClassFilters.Add(ClassWithInterface);
							}
						}
					}
					else
					{
						OutDisallowedClassFilters.Add(Class);
					}
				}
			}
		}
	}
} // namespace OUU::JsonData::Editor::Private

void FJsonDataAssetPathCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const auto EditedStruct = CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct;

	TSharedPtr<IPropertyHandle> PathPropertyHandle;
	if (EditedStruct->IsChildOf(FSoftJsonDataAssetPtr::StaticStruct()))
	{
		PathPropertyHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSoftJsonDataAssetPtr, Path));
	}
	else if (EditedStruct->IsChildOf(FJsonDataAssetPtr::StaticStruct()))
	{
		PathPropertyHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FJsonDataAssetPtr, Path));

		PropertyHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda([PropertyHandle]() {
			if (PropertyHandle->IsValidHandle())
			{
				TArray<void*> RawData;
				PropertyHandle->AccessRawData(RawData);
				for (const auto RawPtr : RawData)
				{
					if (RawPtr)
					{
						static_cast<FJsonDataAssetPtr*>(RawPtr)->NotifyPathChanged();
					}
				}
			}
		}));
	}
	else
	{
		PathPropertyHandle = PropertyHandle;
	}

	const auto ChildHandle =
		PathPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FJsonDataAssetPath, Path)).ToSharedRef();
	const FObjectPropertyBase* ChildProperty = CastField<FObjectPropertyBase>(ChildHandle->GetProperty());

	bool HasClassFilters = false;
	if (const FString* OptClassPath = EditedStruct->FindMetaData(TEXT("JsonDataAssetClass")))
	{
		if (const auto pFilterClass = TSoftClassPtr<UJsonDataAsset>(FSoftObjectPath(*OptClassPath)).LoadSynchronous())
		{
			AllowedClassFilters.Add(pFilterClass);
			HasClassFilters = true;
		}
	}

	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	const auto FilterDelegate =
		FOnShouldFilterAsset::CreateSP(this, &FJsonDataAssetPathCustomization::OnShouldFilterAsset);

	const TArray<FAssetData> ContextOwnerAssets{FJsonAssetReferenceFilter::PassFilterKey()};
	const auto EditWidget =
		SNew(SObjectPropertyEntryBox)
			.ThumbnailPool(CustomizationUtils.GetThumbnailPool())
			.PropertyHandle(ChildHandle)
			.AllowedClass(ChildProperty->PropertyClass)
			.AllowClear(true)
			.OnShouldFilterAsset(FilterDelegate)
			.OwnerAssetDataArray(ContextOwnerAssets)
			.CustomContentSlot()
				[SNew(SBox)
					 .HAlign(HAlign_Left)
					 .VAlign(VAlign_Center)
					 .WidthOverride(22)
					 .HeightOverride(22)
					 .ToolTipText(INVTEXT("Browse to Asset Source in Content Browser"))
						 [SNew(SButton)
							  .ButtonStyle(FAppStyle::Get(), "SimpleButton")
							  .OnClicked_Lambda([PathPropertyHandle]() -> FReply {
								  void* PathAddress = nullptr;
								  if (PathPropertyHandle->GetValueData(OUT PathAddress)
									  == FPropertyAccess::Result::Success)
								  {
									  const FJsonDataAssetPath& Path = *static_cast<FJsonDataAssetPath*>(PathAddress);
									  OUU::Editor::JsonData::ContentBrowser_NavigateToSources({Path});
								  }
								  return FReply::Handled();
							  })
							  .ContentPadding(
								  0)[SNew(SImage)
										 .Image(FSlateIcon("EditorStyle", "Icons.OpenSourceLocation").GetSmallIcon())
										 .ColorAndOpacity(FSlateColor::UseForeground())]]];

	auto IsRecognized = [](const TSharedPtr<FDragDropOperation>& DragDropOperation) -> bool {
		if (DragDropOperation.IsValid() && DragDropOperation->IsOfType<FContentBrowserDataDragDropOp>())
		{
			const auto ContentBrowserDragDropOp = StaticCastSharedPtr<FContentBrowserDataDragDropOp>(DragDropOperation);
			const auto OptJsonPath = OUU::JsonData::Editor::Private::GetFirstJsonPathFromDragDropOp(ContentBrowserDragDropOp);
			return OptJsonPath.IsSet();
		}
		return false;
	};

	auto AllowDrop = [this](const TSharedPtr<FDragDropOperation>& DragDropOperation) {
		if (DragDropOperation.IsValid() && DragDropOperation->IsOfType<FContentBrowserDataDragDropOp>())
		{
			const auto ContentBrowserDragDropOp = StaticCastSharedPtr<FContentBrowserDataDragDropOp>(DragDropOperation);
			auto OptJsonPath = OUU::JsonData::Editor::Private::GetFirstJsonPathFromDragDropOp(ContentBrowserDragDropOp);

			if (OptJsonPath.IsSet())
			{
				const auto JsonPackagePath = OptJsonPath->GetPackagePath();
				const auto ObjectName = OUU::JsonData::Runtime::PackageToObjectName(JsonPackagePath);

				const auto AssetData = IAssetRegistry::Get()->GetAssetByObjectPath(
					FSoftObjectPath(JsonPackagePath + TEXT(".") + ObjectName));

				// Allow dropping if the filter would not filter the asset out
				return this->OnShouldFilterAsset(AssetData) == false;
			}
		}
		return false;
	};

	auto OnDroppedLambda = [PathPropertyHandle](const FGeometry&, const FDragDropEvent& DragDropEvent) -> FReply {
		if (const TSharedPtr<FContentBrowserDataDragDropOp> ContentDragDropOp =
				DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>())
		{
			auto OptJsonPath = OUU::JsonData::Editor::Private::GetFirstJsonPathFromDragDropOp(ContentDragDropOp);
			if (OptJsonPath.IsSet())
			{
				const auto JsonPackagePath = OptJsonPath->GetPackagePath();
				const auto ObjectName = OUU::JsonData::Runtime::PackageToObjectName(JsonPackagePath);
				PathPropertyHandle->SetValueFromFormattedString(JsonPackagePath + TEXT(".") + ObjectName);
				return FReply::Handled();
			}
		}
		return FReply::Unhandled();
	};

	const auto CustomJsonDataDropTarget = SNew(SDropTarget)
										.OnIsRecognized(SDropTarget::FVerifyDrag::CreateLambda(IsRecognized))
										.OnAllowDrop(SDropTarget::FVerifyDrag::CreateLambda(AllowDrop))
										.OnDropped(FOnDrop::CreateLambda(OnDroppedLambda))
										.Content()[EditWidget];

	HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()];
	HeaderRow.ValueContent()[CustomJsonDataDropTarget];

	if (HasClassFilters == false)
	{
		OUU::JsonData::Editor::Private::GetClassFiltersFromPropertyMetadata(
			ChildProperty,
			PropertyHandle->GetProperty(),
			OUT AllowedClassFilters,
			OUT DisallowedClassFilters);
	}
}

void FJsonDataAssetPathCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// do nothing
}

bool FJsonDataAssetPathCustomization::OnShouldFilterAsset(const FAssetData& AssetData) const
{
	const auto* AssetClass = AssetData.GetClass();
	// Blueprint based classes may not be loaded yet, so we need to load it manually
	if (AssetClass == nullptr)
	{
		AssetClass = FSoftClassPath(AssetData.AssetClassPath.ToString()).TryLoadClass<UObject>();
	}

	if (AssetClass)
	{
		bool bAllowedClassFound = false;
		for (auto* AllowClass : AllowedClassFilters)
		{
			if (AssetClass->IsChildOf(AllowClass))
			{
				bAllowedClassFound = true;
				break;
			}
		}
		if (!bAllowedClassFound)
			return true;
		for (auto* DisallowClass : DisallowedClassFilters)
		{
			if (AssetClass->IsChildOf(DisallowClass))
				return true;
		}
	}
	return false;
}
