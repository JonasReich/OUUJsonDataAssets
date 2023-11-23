// Copyright (c) 2023 Jonas Reich & Contributors

#include "JsonLibrary.h"

#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "JsonDataCustomVersions.h"
#include "JsonUtilities.h"
#include "LogJsonDataAsset.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/TextProperty.h"

/** Static switch for types: Select type based on Condition */
template <bool Condition, typename TrueType, typename FalseType>
struct TConditionalType;

template <typename TrueType, typename FalseType>
struct TConditionalType<true, TrueType, FalseType>
{
	using Type = TrueType;
};

template <typename TrueType, typename FalseType>
struct TConditionalType<false, TrueType, FalseType>
{
	using Type = FalseType;
};

namespace OUU::JsonData::Runtime::Private
{
	// The string to return from invalid conversion results.
	const FString InvalidConversionResultString = TEXT("");

	// Use the same name as FJsonObjectConverter to have compatible exports!
	const FString ObjectClassNameKey = "_ClassName";
	const FName NAME_DateTime(TEXT("DateTime"));

} // namespace OUU::JsonData::Runtime::Private
using namespace OUU::JsonData::Runtime::Private;

// Use this to bubble information about change status / skip status through the hierarchy.
struct FOUUPropertyJsonResult
{
	bool bSkip = false;
	TSharedPtr<FJsonValue> Value;

	static FOUUPropertyJsonResult Skip() { return FOUUPropertyJsonResult(true, {}); }
	static FOUUPropertyJsonResult Json(const TSharedPtr<FJsonValue>& Value)
	{
		return FOUUPropertyJsonResult(false, Value);
	}

private:
	FOUUPropertyJsonResult(bool bSkip, const TSharedPtr<FJsonValue>& Value) : bSkip(bSkip), Value(Value) {}
};

struct FJsonLibraryExportHelper
{
	FJsonLibraryExportHelper(
		int64 InCheckFlags,
		int64 InSkipFlags,
		const FOUUJsonLibraryObjectFilter& InSubObjectFilter,
		bool bInOnlyModifiedProperties) :
		DefaultCheckFlags(InCheckFlags),
		DefaultSkipFlags(InSkipFlags),
		SubObjectFilter(InSubObjectFilter),
		bOnlyModifiedProperties(bInOnlyModifiedProperties)
	{
	}

	// Export all properties
	int64 DefaultCheckFlags = 0;
	// Don't skip any properties
	int64 DefaultSkipFlags = 0;

	FOUUJsonLibraryObjectFilter SubObjectFilter;

	bool bOnlyModifiedProperties = true;

	mutable int32 RecursionCounter = 0;

	FJsonObjectConverter::CustomExportCallback GetCustomCallback() const
	{
		FJsonObjectConverter::CustomExportCallback CustomCB;
		CustomCB.BindRaw(this, &FJsonLibraryExportHelper::ObjectJsonCallback);
		return CustomCB;
	}

	bool SkipPropertyMatchingDefaultValues(const FProperty* Property, const void* Value, const void* DefaultValue) const
	{
		if (bOnlyModifiedProperties == false)
		{
			return false;
		}

		if (DefaultValue == nullptr)
		{
			// This property guaranteed to be different.
			// We only pass in nullptr in cases where there is not default to compare (e.g. ptr to array elements in
			// arrays of different size).
			return false;
		}

		if (Property->Identical(Value, DefaultValue))
		{
			return true;
		}

		return false;
	}

	/** Convert property to JSON, assuming either the property is not an array or the value is an individual array
	 * element */
	FOUUPropertyJsonResult ConvertScalarFPropertyToJsonValue(
		FProperty* Property,
		const void* Value,
		const void* DefaultValue,
		int32 Index,
		int64 CheckFlags,
		int64 SkipFlags,
		const FJsonObjectConverter::CustomExportCallback* ExportCb,
		FProperty* OuterProperty,
		const bool SkipIfValueMatchesDefault = true) const
	{
		DECLARE_CYCLE_STAT(
			TEXT("ConvertScalarFPropertyToJsonValue"),
			STAT_ConvertScalarFPropertyToJsonValue,
			STATGROUP_OUUJsonData);

		if (SkipIfValueMatchesDefault && SkipPropertyMatchingDefaultValues(Property, Value, DefaultValue))
		{
			return FOUUPropertyJsonResult::Skip();
		}

		// See if there's a custom export callback first, so it can override default behavior
		if (ExportCb && ExportCb->IsBound())
		{
			TSharedPtr<FJsonValue> CustomValue = ExportCb->Execute(Property, Value);
			if (CustomValue.IsValid())
			{
				return FOUUPropertyJsonResult::Json(CustomValue);
			}
			// fall through to default cases
		}

		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			// export enums as strings
			UEnum* EnumDef = EnumProperty->GetEnum();
			FString StringValue = EnumDef->GetAuthoredNameStringByValue(
				EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
			return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueString>(StringValue));
		}
		else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			// see if it's an enum
			UEnum* EnumDef = NumericProperty->GetIntPropertyEnum();
			if (EnumDef != nullptr)
			{
				// export enums as strings
				FString StringValue =
					EnumDef->GetAuthoredNameStringByValue(NumericProperty->GetSignedIntPropertyValue(Value));
				return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueString>(StringValue));
			}

			// We want to export numbers as numbers
			if (NumericProperty->IsFloatingPoint())
			{
				return FOUUPropertyJsonResult::Json(
					MakeShared<FJsonValueNumber>(NumericProperty->GetFloatingPointPropertyValue(Value)));
			}
			else if (NumericProperty->IsInteger())
			{
				return FOUUPropertyJsonResult::Json(
					MakeShared<FJsonValueNumber>(NumericProperty->GetSignedIntPropertyValue(Value)));
			}

			// fall through to default
		}
		else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			// Export booleans as booleans
			return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(Value)));
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(Value)));
		}
		else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			return FOUUPropertyJsonResult::Json(
				MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(Value).ToString()));
		}
		else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			FScriptArrayHelper Helper(ArrayProperty, Value);

			if (Helper.Num() > 0)
			{
				TOptional<FStructOnScope> OptDefaultValue;

				if (const auto ValueStructProp = CastField<FStructProperty>(ArrayProperty->Inner))
				{
					OptDefaultValue = FStructOnScope(ValueStructProp->Struct);
				}
				void* DefaultElemPtr = OptDefaultValue.IsSet() ? OptDefaultValue.GetValue().GetStructMemory() : nullptr;

				for (int32 i = 0, n = Helper.Num(); i < n; ++i)
				{
					auto Elem = UPropertyToJsonValue(
						ArrayProperty->Inner,
						Helper.GetRawPtr(i),
						DefaultElemPtr,
						CheckFlags & (~CPF_ParmFlags),
						SkipFlags,
						ExportCb,
						ArrayProperty,
						false);

					if (Elem.Value.IsValid())
					{
						// add to the array
						Out.Push(Elem.Value);
					}
				}
			}

			return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueArray>(Out));
		}
		else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			FScriptSetHelper Helper(SetProperty, Value);
			if (Helper.Num() > 0)
			{
				TOptional<FStructOnScope> OptDefaultValue;

				if (const auto ValueStructProp = CastField<FStructProperty>(SetProperty->ElementProp))
				{
					OptDefaultValue = FStructOnScope(ValueStructProp->Struct);
				}
				void* DefaultElemPtr = OptDefaultValue.IsSet() ? OptDefaultValue.GetValue().GetStructMemory() : nullptr;

				for (int32 i = 0, n = Helper.Num(); n; ++i)
				{
					if (Helper.IsValidIndex(i))
					{
						auto Elem = UPropertyToJsonValue(
							SetProperty->ElementProp,
							Helper.GetElementPtr(i),
							DefaultElemPtr,
							CheckFlags & (~CPF_ParmFlags),
							SkipFlags,
							ExportCb,
							SetProperty,
							false);

						if (Elem.Value.IsValid())
						{
							// add to the array
							Out.Push(Elem.Value);
						}

						--n;
					}
				}
			}

			return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueArray>(Out));
		}
		else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			FScriptMapHelper Helper(MapProperty, Value);

			if (Helper.Num() > 0)
			{
				TOptional<FStructOnScope> OptDefaultKey;
				TOptional<FStructOnScope> OptDefaultValue;
				if (const auto KeyStructProp = CastField<FStructProperty>(MapProperty->KeyProp))
				{
					OptDefaultKey = FStructOnScope(KeyStructProp->Struct);
				}
				if (const auto ValueStructProp = CastField<FStructProperty>(MapProperty->ValueProp))
				{
					OptDefaultValue = FStructOnScope(ValueStructProp->Struct);
				}
				void* DefaultKeyPtr = OptDefaultKey.IsSet() ? OptDefaultKey.GetValue().GetStructMemory() : nullptr;
				void* DefaultValuePtr =
					OptDefaultValue.IsSet() ? OptDefaultValue.GetValue().GetStructMemory() : nullptr;

				for (int32 i = 0, n = Helper.Num(); n; ++i)
				{
					if (Helper.IsValidIndex(i))
					{
						auto KeyElement = UPropertyToJsonValue(
							MapProperty->KeyProp,
							Helper.GetKeyPtr(i),
							DefaultKeyPtr,
							CheckFlags & (~CPF_ParmFlags),
							SkipFlags,
							ExportCb,
							MapProperty,
							false);
						auto ValueElement = UPropertyToJsonValue(
							MapProperty->ValueProp,
							Helper.GetValuePtr(i),
							DefaultValuePtr,
							CheckFlags & (~CPF_ParmFlags),
							SkipFlags,
							ExportCb,
							MapProperty,
							false);

						FString KeyString;
						if (!KeyElement.Value->TryGetString(KeyString))
						{
							MapProperty->KeyProp
								->ExportTextItem_Direct(KeyString, Helper.GetKeyPtr(i), nullptr, nullptr, 0);
							if (KeyString.IsEmpty())
							{
								UE_LOG(
									LogJsonDataAsset,
									Error,
									TEXT("Unable to convert key to string for property %s."),
									*MapProperty->GetAuthoredName())
								KeyString = FString::Printf(TEXT("Unparsed Key %d"), i);
							}
						}

						// Coerce camelCase map keys for Enum/FName properties
						if (CastField<FEnumProperty>(MapProperty->KeyProp)
							|| CastField<FNameProperty>(MapProperty->KeyProp))
						{
							KeyString = FJsonObjectConverter::StandardizeCase(KeyString);
						}
						Out->SetField(KeyString, ValueElement.Value);

						--n;
					}
				}
			}

			return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueObject>(Out));
		}
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			UScriptStruct::ICppStructOps* TheCppStructOps = StructProperty->Struct->GetCppStructOps();
			// Intentionally exclude the JSON Object wrapper, which specifically needs to export JSON in an object
			// representation instead of a string
			if (StructProperty->Struct != FJsonObjectWrapper::StaticStruct() && TheCppStructOps
				&& TheCppStructOps->HasExportTextItem())
			{
				FString OutValueStr;
				TheCppStructOps->ExportTextItem(OutValueStr, Value, nullptr, nullptr, PPF_None, nullptr);
				return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueString>(OutValueStr));
			}

			// GRIMLORE Start dlehn: Gameplay tags and containers for WHATEVER REASON have an IMPORTTextItem function
			// but no EXPORTTextItem, so we have to handle this manually.
			if (StructProperty->Struct->IsChildOf(FGameplayTag::StaticStruct()))
			{
				const auto& Tag = *StaticCast<const FGameplayTag*>(Value);
				return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			else if (StructProperty->Struct->IsChildOf(FGameplayTagContainer::StaticStruct()))
			{
				const auto& Container = *StaticCast<const FGameplayTagContainer*>(Value);
				TArray<TSharedPtr<FJsonValue>> Values;
				Values.Reserve(Container.Num());
				for (const auto& Tag : Container)
				{
					Values.Add(MakeShared<FJsonValueString>(Tag.ToString()));
				}

				return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueArray>(Values));
			}
			// GRIMLORE End

			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			bool MinimumOneValueSet = false;
			if (UStructToJsonObject(
					StructProperty->Struct,
					Value,
					DefaultValue,
					Out,
					OUT MinimumOneValueSet,
					CheckFlags & (~CPF_ParmFlags),
					SkipFlags,
					ExportCb))
			{
				return (MinimumOneValueSet || bOnlyModifiedProperties == false || SkipIfValueMatchesDefault == false)
					? FOUUPropertyJsonResult::Json(MakeShared<FJsonValueObject>(Out))
					: FOUUPropertyJsonResult::Skip();
			}
		}
		else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			// Instanced properties should be copied by value, while normal UObject* properties should output as asset
			// references
			UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
			if (Object
				&& (ObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance)
					|| (OuterProperty && OuterProperty->HasAnyPropertyFlags(CPF_PersistentInstance))))
			{
				TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

				Out->SetStringField(ObjectClassNameKey, Object->GetClass()->GetPathName());
				bool MinimumOneValueSet = false;
				if (UStructToJsonObject(
						ObjectProperty->GetObjectPropertyValue(Value)->GetClass(),
						Object,
						Object->GetClass()->GetDefaultObject(),
						Out,
						OUT MinimumOneValueSet,
						CheckFlags,
						SkipFlags,
						ExportCb))
				{
					UObject* DefaultObject =
						DefaultValue ? ObjectProperty->GetObjectPropertyValue(DefaultValue) : nullptr;
					// No class or different class
					const bool bDifferentClass =
						DefaultObject == nullptr || Object->GetClass() != DefaultObject->GetClass();

					TSharedRef<FJsonValueObject> JsonObject = MakeShared<FJsonValueObject>(Out);
					JsonObject->Type = EJson::Object;
					return (MinimumOneValueSet || bOnlyModifiedProperties == false || bDifferentClass
							|| SkipIfValueMatchesDefault == false)
						? FOUUPropertyJsonResult::Json(JsonObject)
						: FOUUPropertyJsonResult::Skip();
				}
			}
			else
			{
				FString StringValue;
				Property->ExportTextItem_Direct(StringValue, Value, nullptr, nullptr, PPF_None);
				return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueString>(StringValue));
			}
		}
		else
		{
			// Default to export as string for everything else
			FString StringValue;
			Property->ExportTextItem_Direct(StringValue, Value, nullptr, nullptr, PPF_None);
			return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueString>(StringValue));
		}

		// invalid
		return FOUUPropertyJsonResult::Json(TSharedPtr<FJsonValue>());
	}

	FOUUPropertyJsonResult UPropertyToJsonValue(
		FProperty* Property,
		const void* Value,
		const void* DefaultValue,
		int64 CheckFlags,
		int64 SkipFlags,
		const FJsonObjectConverter::CustomExportCallback* ExportCb,
		FProperty* OuterProperty = nullptr,
		const bool SkipIfValueMatchesDefault = true) const
	{
		if (SkipIfValueMatchesDefault && SkipPropertyMatchingDefaultValues(Property, Value, DefaultValue))
		{
			return FOUUPropertyJsonResult::Skip();
		}

		if (Property->ArrayDim == 1)
		{
			return ConvertScalarFPropertyToJsonValue(
				Property,
				Value,
				DefaultValue,
				0,
				CheckFlags,
				SkipFlags,
				ExportCb,
				OuterProperty,
				SkipIfValueMatchesDefault);
		}

		TArray<TSharedPtr<FJsonValue>> Array;
		for (int Index = 0; Index != Property->ArrayDim; ++Index)
		{
			auto ArrayElement = ConvertScalarFPropertyToJsonValue(
				Property,
				StaticCast<const char*>(Value) + Index * Property->ElementSize,
				DefaultValue ? StaticCast<const char*>(DefaultValue) + Index * Property->ElementSize : nullptr,
				Index,
				CheckFlags,
				SkipFlags,
				ExportCb,
				OuterProperty,
				false);

			// We can't really skip individual array elements, can we?
			// Also, we already assume, something is changed in here, so we have to serialize the entire array
			ensure(ArrayElement.bSkip == false);

			Array.Add(ArrayElement.Value);
		}
		return FOUUPropertyJsonResult::Json(MakeShared<FJsonValueArray>(Array));
	}

	bool UStructToJsonAttributes(
		const UStruct* StructDefinition,
		const void* Struct,
		const void* DefaultStruct,
		TMap<FString, TSharedPtr<FJsonValue>>& OutJsonAttributes,
		bool& OutMinimumOneValueSet,
		int64 CheckFlags,
		int64 SkipFlags,
		const FJsonObjectConverter::CustomExportCallback* ExportCb) const
	{
		DECLARE_CYCLE_STAT(TEXT("UStructToJsonAttributes"), STAT_UStructToJsonAttributes, STATGROUP_OUUJsonData);

		OutMinimumOneValueSet = false;

		if (SkipFlags == 0)
		{
			// If we have no specified skip flags, skip deprecated, transient and skip serialization by default when
			// writing
			SkipFlags |= CPF_Deprecated | CPF_Transient;
		}

		if (StructDefinition == FJsonObjectWrapper::StaticStruct())
		{
			// Just copy it into the object
			const FJsonObjectWrapper* ProxyObject = StaticCast<const FJsonObjectWrapper*>(Struct);

			if (ProxyObject->JsonObject.IsValid())
			{
				OutJsonAttributes = ProxyObject->JsonObject->Values;
				OutMinimumOneValueSet = true;
			}
			return true;
		}

		for (TFieldIterator<FProperty> It(StructDefinition); It; ++It)
		{
			FProperty* Property = *It;

			// Check to see if we should ignore this property
			if (CheckFlags != 0 && !Property->HasAnyPropertyFlags(CheckFlags))
			{
				continue;
			}
			if (Property->HasAnyPropertyFlags(SkipFlags))
			{
				continue;
			}

			FString VariableName = FJsonObjectConverter::StandardizeCase(Property->GetAuthoredName());
			const void* Value = Property->ContainerPtrToValuePtr<uint8>(Struct);
			const void* DefaultValue = DefaultStruct ? Property->ContainerPtrToValuePtr<uint8>(DefaultStruct) : nullptr;

			// convert the property to a FJsonValue
			const auto PropertyResult =
				UPropertyToJsonValue(Property, Value, DefaultValue, CheckFlags, SkipFlags, ExportCb);
			if (PropertyResult.bSkip)
				continue;

			OutMinimumOneValueSet = true;

			TSharedPtr<FJsonValue> JsonValue = PropertyResult.Value;
			if (!JsonValue.IsValid())
			{
				const FFieldClass* PropClass = Property->GetClass();
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("UStructToJsonAttributes - Unhandled property type '%s': %s"),
					*PropClass->GetName(),
					*Property->GetPathName());
				return false;
			}

			// set the value on the output object
			OutJsonAttributes.Add(VariableName, JsonValue);
		}

		return true;
	}

	// Implementation copied from FJsonObjectConverter::ObjectJsonCallback
	// Modified to support stop class
	TSharedPtr<FJsonValue> ObjectJsonCallback(FProperty* Property, const void* Value) const { return {}; }

	bool UStructToJsonObject(
		const UStruct* StructDefinition,
		const void* Struct,
		const void* DefaultStruct,
		TSharedRef<FJsonObject> OutJsonObject,
		bool& OutMinimumOneValueSet,
		int64 CheckFlags,
		int64 SkipFlags,
		const FJsonObjectConverter::CustomExportCallback* ExportCb) const
	{
		return UStructToJsonAttributes(
			StructDefinition,
			Struct,
			DefaultStruct,
			OutJsonObject->Values,
			OutMinimumOneValueSet,
			CheckFlags,
			SkipFlags,
			ExportCb);
	}

	TSharedPtr<FJsonObject> ConvertStructToJsonObject(const void* Data, const void* DefaultData, const UStruct* Struct)
		const
	{
		const FJsonObjectConverter::CustomExportCallback CustomCB = GetCustomCallback();
		TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		bool MinimumOneValueSet = false;
		if (UStructToJsonObject(
				Struct,
				Data,
				DefaultData,
				OUT JsonObject,
				OUT MinimumOneValueSet,
				DefaultCheckFlags,
				DefaultSkipFlags,
				&CustomCB))
		{
			return JsonObject;
		}
		return TSharedPtr<FJsonObject>();
	}

	TSharedPtr<FJsonObject> ConvertObjectToJsonObject(const UObject* Object) const
	{
		const FJsonObjectConverter::CustomExportCallback CustomCB = GetCustomCallback();
		TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		bool MinimumOneValueSet = false;
		if (UStructToJsonObject(
				Object->GetClass(),
				Object,
				Object->GetClass()->GetDefaultObject(),
				OUT JsonObject,
				OUT MinimumOneValueSet,
				DefaultCheckFlags,
				DefaultSkipFlags,
				&CustomCB))
		{
			return JsonObject;
		}
		return TSharedPtr<FJsonObject>();
	}

	TSharedPtr<FJsonValue> ConvertPropertyToJsonValue(const void* Data, const void* DefaultData, FProperty* Property)
		const
	{
		const FJsonObjectConverter::CustomExportCallback CustomCB = GetCustomCallback();
		auto Result = UPropertyToJsonValue(Property, Data, DefaultData, DefaultCheckFlags, DefaultSkipFlags, &CustomCB);
		if (Result.Value)
		{
			return Result.Value;
		}
		return TSharedPtr<FJsonValueNull>();
	}

	template <
		bool bPrettyPrint,
		class PrintPolicy =
			typename TConditionalType<bPrettyPrint, TPrettyJsonPrintPolicy<TCHAR>, TCondensedJsonPrintPolicy<TCHAR>>::
				Type>
	bool UStructToJsonObjectStringInternal(const TSharedRef<FJsonObject>& JsonObject, FString& OutJsonString)
	{
		constexpr int32 Indent = 4;
		TSharedRef<TJsonWriter<TCHAR, PrintPolicy>> JsonWriter =
			TJsonWriterFactory<TCHAR, PrintPolicy>::Create(&OutJsonString, Indent);
		const bool bSuccess = FJsonSerializer::Serialize(JsonObject, JsonWriter);
		JsonWriter->Close();
		return bSuccess;
	}

	template <bool bPrettyPrint>
	FString ConvertObjectToString(const UObject* Object)
	{
		const TSharedPtr<FJsonObject> JsonObject = ConvertObjectToJsonObject(Object);
		if (JsonObject.IsValid())
		{
			FString JsonString;
			if (UStructToJsonObjectStringInternal<bPrettyPrint>(JsonObject.ToSharedRef(), OUT JsonString))
			{
				return JsonString;
			}
			else
			{
				UE_LOG(LogJsonDataAsset, Warning, TEXT("ConvertObjectToString - Unable to write out JSON"));
			}
		}

		return OUU::JsonData::Runtime::Private::InvalidConversionResultString;
	}
};

struct FJsonLibraryImportHelper
{
	// clang-format off
	bool JsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, const FArchive& VersionLoadingArchive, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason);
	bool JsonAttributesToUStructWithContainer(const TMap< FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, const UStruct* ContainerStruct, void* Container, const FArchive& VersionLoadingArchive, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason);
	// clang-format on

	bool JsonValueToUProperty(
		const TSharedPtr<FJsonValue>& JsonValue,
		FProperty* Property,
		void* OutValue,
		const FArchive& VersionLoadingArchive,
		int64 CheckFlags,
		int64 SkipFlags,
		const bool bStrictMode = false,
		FText* OutFailReason = nullptr)
	{
		return JsonValueToFPropertyWithContainer(
			JsonValue,
			Property,
			OutValue,
			nullptr,
			nullptr,
			VersionLoadingArchive,
			CheckFlags,
			SkipFlags,
			bStrictMode,
			OutFailReason);
	}

	bool JsonObjectToUStruct(
		const TSharedRef<FJsonObject>& JsonObject,
		const UStruct* StructDefinition,
		void* OutStruct,
		const FArchive& VersionLoadingArchive,
		int64 CheckFlags,
		int64 SkipFlags,
		const bool bStrictMode = false,
		FText* OutFailReason = nullptr)
	{
		return JsonAttributesToUStruct(
			JsonObject->Values,
			StructDefinition,
			OutStruct,
			VersionLoadingArchive,
			CheckFlags,
			SkipFlags,
			bStrictMode,
			OutFailReason);
	}

	bool JsonAttributesToUStruct(
		const TMap<FString, TSharedPtr<FJsonValue>>& JsonAttributes,
		const UStruct* StructDefinition,
		void* OutStruct,
		const FArchive& VersionLoadingArchive,
		int64 CheckFlags,
		int64 SkipFlags,
		const bool bStrictMode = false,
		FText* OutFailReason = nullptr)
	{
		return JsonAttributesToUStructWithContainer(
			JsonAttributes,
			StructDefinition,
			OutStruct,
			StructDefinition,
			OutStruct,
			VersionLoadingArchive,
			CheckFlags,
			SkipFlags,
			bStrictMode,
			OutFailReason);
	}

	/** Convert JSON to property, assuming either the property is not an array or the value is an individual array
	 * element */
	bool ConvertScalarJsonValueToFPropertyWithContainer(
		const TSharedPtr<FJsonValue>& JsonValue,
		FProperty* Property,
		void* OutValue,
		const UStruct* ContainerStruct,
		void* Container,
		const FArchive& VersionLoadingArchive,
		int64 CheckFlags,
		int64 SkipFlags,
		const bool bStrictMode,
		FText* OutFailReason)
	{
		DECLARE_CYCLE_STAT(
			TEXT("ConvertScalarJsonValueToFPropertyWithContainer"),
			STAT_ConvertScalarJsonValueToFPropertyWithContainer,
			STATGROUP_OUUJsonData);

		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			if (JsonValue->Type == EJson::String)
			{
				// see if we were passed a string for the enum
				const UEnum* Enum = EnumProperty->GetEnum();
				check(Enum);
				FString StrValue = JsonValue->AsString();
				int64 IntValue = Enum->GetValueByName(FName(*StrValue), EGetByNameFlags::CheckAuthoredName);
				if (IntValue == INDEX_NONE)
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import enum %s from string value %s for property %s"),
						*Enum->CppType,
						*StrValue,
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import enum {0} from string value {1} for property {2}"),
							FText::FromString(Enum->CppType),
							FText::FromString(StrValue),
							FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
				EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, IntValue);
			}
			else
			{
				// AsNumber will log an error for completely inappropriate types (then give us a default)
				EnumProperty->GetUnderlyingProperty()
					->SetIntPropertyValue(OutValue, StaticCast<int64>(JsonValue->AsNumber()));
			}
		}
		else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsEnum() && JsonValue->Type == EJson::String)
			{
				// see if we were passed a string for the enum
				const UEnum* Enum = NumericProperty->GetIntPropertyEnum();
				check(Enum); // should be assured by IsEnum()
				FString StrValue = JsonValue->AsString();
				int64 IntValue = Enum->GetValueByName(FName(*StrValue), EGetByNameFlags::CheckAuthoredName);
				if (IntValue == INDEX_NONE)
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import enum %s from numeric value %s for property %s"),
						*Enum->CppType,
						*StrValue,
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import enum {0} from numeric value {1} for property {2}"),
							FText::FromString(Enum->CppType),
							FText::FromString(StrValue),
							FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
				NumericProperty->SetIntPropertyValue(OutValue, IntValue);
			}
			else if (NumericProperty->IsFloatingPoint())
			{
				// AsNumber will log an error for completely inappropriate types (then give us a default)
				NumericProperty->SetFloatingPointPropertyValue(OutValue, JsonValue->AsNumber());
			}
			else if (NumericProperty->IsInteger())
			{
				if (JsonValue->Type == EJson::String)
				{
					// parse string -> int64 ourselves so we don't lose any precision going through AsNumber (aka
					// double)
					NumericProperty->SetIntPropertyValue(OutValue, FCString::Atoi64(*JsonValue->AsString()));
				}
				else
				{
					// AsNumber will log an error for completely inappropriate types (then give us a default)
					NumericProperty->SetIntPropertyValue(OutValue, static_cast<int64>(JsonValue->AsNumber()));
				}
			}
			else
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import json value into %s numeric property %s"),
					*Property->GetClass()->GetName(),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import json value into {0} numeric property {1}"),
						FText::FromString(Property->GetClass()->GetName()),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
		else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			// AsBool will log an error for completely inappropriate types (then give us a default)
			BoolProperty->SetPropertyValue(OutValue, JsonValue->AsBool());
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			// AsString will log an error for completely inappropriate types (then give us a default)
			StringProperty->SetPropertyValue(OutValue, JsonValue->AsString());
		}
		else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (JsonValue->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> ArrayValue = JsonValue->AsArray();
				int32 ArrLen = ArrayValue.Num();

				// make the output array size match
				FScriptArrayHelper Helper(ArrayProperty, OutValue);
				Helper.Resize(ArrLen);

				// set the property values
				for (int32 i = 0; i < ArrLen; ++i)
				{
					const TSharedPtr<FJsonValue>& ArrayValueItem = ArrayValue[i];
					if (ArrayValueItem.IsValid() && !ArrayValueItem->IsNull())
					{
						if (!JsonValueToFPropertyWithContainer(
								ArrayValueItem,
								ArrayProperty->Inner,
								Helper.GetRawPtr(i),
								ContainerStruct,
								Container,
								VersionLoadingArchive,
								CheckFlags & (~CPF_ParmFlags),
								SkipFlags,
								bStrictMode,
								OutFailReason))
						{
							UE_LOG(
								LogJsonDataAsset,
								Error,
								TEXT("JsonValueToUProperty - Unable to import Array element %d for property %s"),
								i,
								*Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(
									INVTEXT("Unable to import Array element {0} for property {1}\n{2}"),
									FText::AsNumber(i),
									FText::FromString(Property->GetAuthoredName()),
									*OutFailReason);
							}
							return false;
						}
					}
				}
			}
			else
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import non-array JSON value into Array property %s"),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import non-array JSON value into Array property {0}"),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
		else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			if (JsonValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();

				FScriptMapHelper Helper(MapProperty, OutValue);

				check(ObjectValue);

				int32 MapSize = ObjectValue->Values.Num();
				Helper.EmptyValues(MapSize);

				// set the property values
				for (const auto& Entry : ObjectValue->Values)
				{
					if (Entry.Value.IsValid() && !Entry.Value->IsNull())
					{
						int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();

						TSharedPtr<FJsonValueString> TempKeyValue = MakeShared<FJsonValueString>(Entry.Key);

						if (!JsonValueToFPropertyWithContainer(
								TempKeyValue,
								MapProperty->KeyProp,
								Helper.GetKeyPtr(NewIndex),
								ContainerStruct,
								Container,
								VersionLoadingArchive,
								CheckFlags & (~CPF_ParmFlags),
								SkipFlags,
								bStrictMode,
								OutFailReason))
						{
							UE_LOG(
								LogJsonDataAsset,
								Error,
								TEXT("JsonValueToUProperty - Unable to import Map element %s key for property %s"),
								*Entry.Key,
								*Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(
									INVTEXT("Unable to import Map element {0} key for property {1}\n{2}"),
									FText::FromString(Entry.Key),
									FText::FromString(Property->GetAuthoredName()),
									*OutFailReason);
							}
							return false;
						}

						if (!JsonValueToFPropertyWithContainer(
								Entry.Value,
								MapProperty->ValueProp,
								Helper.GetValuePtr(NewIndex),
								ContainerStruct,
								Container,
								VersionLoadingArchive,
								CheckFlags & (~CPF_ParmFlags),
								SkipFlags,
								bStrictMode,
								OutFailReason))
						{
							UE_LOG(
								LogJsonDataAsset,
								Error,
								TEXT("JsonValueToUProperty - Unable to import Map element %s value for property %s"),
								*Entry.Key,
								*Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(
									INVTEXT("Unable to import Map element {0} value for property {1}\n{2}"),
									FText::FromString(Entry.Key),
									FText::FromString(Property->GetAuthoredName()),
									*OutFailReason);
							}
							return false;
						}
					}
				}

				Helper.Rehash();
			}
			else
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import non-object JSON value into Map property %s"),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import non-object JSON value into Map property {0}"),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
		else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			if (JsonValue->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> ArrayValue = JsonValue->AsArray();
				int32 ArrLen = ArrayValue.Num();

				FScriptSetHelper Helper(SetProperty, OutValue);
				Helper.EmptyElements(ArrLen);

				// set the property values
				for (int32 i = 0; i < ArrLen; ++i)
				{
					const TSharedPtr<FJsonValue>& ArrayValueItem = ArrayValue[i];
					if (ArrayValueItem.IsValid() && !ArrayValueItem->IsNull())
					{
						int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
						if (!JsonValueToFPropertyWithContainer(
								ArrayValueItem,
								SetProperty->ElementProp,
								Helper.GetElementPtr(NewIndex),
								ContainerStruct,
								Container,
								VersionLoadingArchive,
								CheckFlags & (~CPF_ParmFlags),
								SkipFlags,
								bStrictMode,
								OutFailReason))
						{
							UE_LOG(
								LogJsonDataAsset,
								Error,
								TEXT("JsonValueToUProperty - Unable to import Set element %d for property %s"),
								i,
								*Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(
									INVTEXT("Unable to import Set element {0} for property {1}\n{2}"),
									FText::AsNumber(i),
									FText::FromString(Property->GetAuthoredName()),
									*OutFailReason);
							}
							return false;
						}
					}
				}

				Helper.Rehash();
			}
			else
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import non-array JSON value into Set property %s"),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import non-array JSON value into Set property {0}"),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
		else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			if (JsonValue->Type == EJson::String)
			{
				// assume this string is already localized, so import as invariant
				TextProperty->SetPropertyValue(OutValue, FText::FromString(JsonValue->AsString()));
			}
			else if (JsonValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
				check(Obj.IsValid()); // should not fail if Type == EJson::Object

				// import the sub-value as a culture invariant string
				FText Text;
				if (!FJsonObjectConverter::GetTextFromObject(Obj.ToSharedRef(), Text))
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON object with invalid keys into Text property "
							 "%s"),
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON object with invalid keys into Text property {0}"),
							FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
				TextProperty->SetPropertyValue(OutValue, Text);
			}
			else
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import JSON value that is neither string nor object into "
						 "Text property %s"),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import JSON value that is neither string nor object into Text property {0}"),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (JsonValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
				check(Obj.IsValid()); // should not fail if Type == EJson::Object
				if (!JsonAttributesToUStructWithContainer(
						Obj->Values,
						StructProperty->Struct,
						OutValue,
						ContainerStruct,
						Container,
						VersionLoadingArchive,
						CheckFlags & (~CPF_ParmFlags),
						SkipFlags,
						bStrictMode,
						OutFailReason))
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON object into %s property %s"),
						*StructProperty->Struct->GetAuthoredName(),
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON object into {0} property {1}\n{2}"),
							FText::FromString(StructProperty->Struct->GetAuthoredName()),
							FText::FromString(Property->GetAuthoredName()),
							*OutFailReason);
					}
					return false;
				}
			}
			else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_LinearColor)
			{
				FLinearColor& ColorOut = *StaticCast<FLinearColor*>(OutValue);
				FString ColorString = JsonValue->AsString();

				FColor IntermediateColor;
				IntermediateColor = FColor::FromHex(ColorString);

				ColorOut = IntermediateColor;
			}
			else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_Color)
			{
				FColor& ColorOut = *StaticCast<FColor*>(OutValue);
				FString ColorString = JsonValue->AsString();

				ColorOut = FColor::FromHex(ColorString);
			}
			else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_DateTime)
			{
				FString DateString = JsonValue->AsString();
				FDateTime& DateTimeOut = *StaticCast<FDateTime*>(OutValue);
				if (DateString == TEXT("min"))
				{
					// min representable value for our date struct. Actual date may vary by platform (this is used for
					// sorting)
					DateTimeOut = FDateTime::MinValue();
				}
				else if (DateString == TEXT("max"))
				{
					// max representable value for our date struct. Actual date may vary by platform (this is used for
					// sorting)
					DateTimeOut = FDateTime::MaxValue();
				}
				else if (DateString == TEXT("now"))
				{
					// this value's not really meaningful from JSON serialization (since we don't know timezone) but
					// handle it anyway since we're handling the other keywords
					DateTimeOut = FDateTime::UtcNow();
				}
				else if (FDateTime::ParseIso8601(*DateString, DateTimeOut))
				{
					// ok
				}
				else if (FDateTime::Parse(DateString, DateTimeOut))
				{
					// ok
				}
				else
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON string into DateTime property %s"),
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON string into DateTime property {0}"),
							FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
			}
			else if (
				JsonValue->Type == EJson::String && StructProperty->Struct->GetCppStructOps()
				&& StructProperty->Struct->GetCppStructOps()->HasImportTextItem())
			{
				UScriptStruct::ICppStructOps* TheCppStructOps = StructProperty->Struct->GetCppStructOps();

				FString ImportTextString = JsonValue->AsString();
				const TCHAR* ImportTextPtr = *ImportTextString;
				if (!TheCppStructOps->ImportTextItem(ImportTextPtr, OutValue, PPF_None, nullptr, (FOutputDevice*)GWarn))
				{
					// Fall back to trying the tagged property approach if custom ImportTextItem couldn't get it done
					if (Property->ImportText_Direct(ImportTextPtr, OutValue, nullptr, PPF_None) == nullptr)
					{
						UE_LOG(
							LogJsonDataAsset,
							Error,
							TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s"),
							*StructProperty->Struct->GetAuthoredName(),
							*Property->GetAuthoredName());
						if (OutFailReason)
						{
							*OutFailReason = FText::Format(
								INVTEXT("Unable to import JSON string into {0} property {1}"),
								FText::FromString(StructProperty->Struct->GetAuthoredName()),
								FText::FromString(Property->GetAuthoredName()));
						}
						return false;
					}
				}
			}
			else if (JsonValue->Type == EJson::String)
			{
				FString ImportTextString = JsonValue->AsString();
				const TCHAR* ImportTextPtr = *ImportTextString;
				if (Property->ImportText_Direct(ImportTextPtr, OutValue, nullptr, PPF_None) == nullptr)
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s"),
						*StructProperty->Struct->GetAuthoredName(),
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON string into {0} property {1}"),
							FText::FromString(StructProperty->Struct->GetAuthoredName()),
							FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
			}
			else
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import JSON value that is neither string nor object into %s "
						 "property %s"),
					*StructProperty->Struct->GetAuthoredName(),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import JSON value that is neither string nor object into {0} property {1}"),
						FText::FromString(StructProperty->Struct->GetAuthoredName()),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
		else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			if (JsonValue->Type == EJson::Object)
			{
				UObject* Outer = GetTransientPackage();
				if (ContainerStruct->IsChildOf(UObject::StaticClass()))
				{
					Outer = StaticCast<UObject*>(Container);
				}

				TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
				UClass* PropertyClass = ObjectProperty->PropertyClass;

				// If a specific subclass was stored in the JSON, use that instead of the PropertyClass
				FString ClassString = Obj->GetStringField(ObjectClassNameKey);
				Obj->RemoveField(ObjectClassNameKey);
				if (!ClassString.IsEmpty())
				{
					// ReSharper disable once CppTooWideScope
					UClass* FoundClass = FPackageName::IsShortPackageName(ClassString)
						? FindFirstObject<UClass>(*ClassString)
						: UClass::TryFindTypeSlow<UClass>(ClassString);
					if (FoundClass)
					{
						PropertyClass = FoundClass;

						// GRIMLORE Start dlehn: If stored class does not match expected class, make sure to create the
						// correct type
						if (PropertyClass->IsChildOf(ObjectProperty->PropertyClass) == false
							|| PropertyClass->HasAnyClassFlags(CLASS_Abstract))
						{
							UE_LOG(
								LogJsonDataAsset,
								Warning,
								TEXT("JsonValueToUProperty - JSON object class %s saved in property %s on object %s is "
									 "not valid for a property of type %s. "
									 "Will try to load as default class instead."),
								*PropertyClass->GetAuthoredName(),
								*Property->GetAuthoredName(),
								*Outer->GetPathName(),
								*ObjectProperty->PropertyClass->GetAuthoredName());

							PropertyClass = ObjectProperty->PropertyClass;
						}
					}
					else
					{
						UE_LOG(
							LogJsonDataAsset,
							Warning,
							TEXT("JsonValueToUProperty - JSON object class %s saved in property %s of type %s on "
								 "object %s does not exist. Will try to load as default class instead."),
							*ClassString,
							*Property->GetAuthoredName(),
							*ObjectProperty->PropertyClass->GetAuthoredName(),
							*Outer->GetPathName());
						// GRIMLORE End
					}
				}

				// GRIMLORE Start dlehn: Property class may not be valid, so we will not create an object.
				if (PropertyClass->HasAnyClassFlags(CLASS_Abstract))
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON object of class %s into property %s on "
							 "object %s because the class is abstract."),
						*PropertyClass->GetAuthoredName(),
						*Property->GetAuthoredName(),
						*Outer->GetPathName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON object of class {0} into property {1} because the class is "
									"abstract.\n{2}"),
							FText::FromString(PropertyClass->GetAuthoredName()),
							FText::FromString(Property->GetAuthoredName()),
							*OutFailReason);
					}

					ObjectProperty->SetObjectPropertyValue(OutValue, nullptr);

					// We cannot return false here, otherwise loading of the object will be cancelled entirely.
					return true;
				}
				// GRIMLORE End

				UObject* createdObj = StaticAllocateObject(
					PropertyClass,
					Outer,
					NAME_None,
					EObjectFlags::RF_NoFlags,
					EInternalObjectFlags::None,
					false);
				(*PropertyClass->ClassConstructor)(
					FObjectInitializer(createdObj, PropertyClass->ClassDefaultObject, EObjectInitializerOptions::None));

				ObjectProperty->SetObjectPropertyValue(OutValue, createdObj);

				check(Obj.IsValid()); // should not fail if Type == EJson::Object
				if (!JsonAttributesToUStructWithContainer(
						Obj->Values,
						PropertyClass,
						createdObj,
						PropertyClass,
						createdObj,
						VersionLoadingArchive,
						CheckFlags & (~CPF_ParmFlags),
						SkipFlags,
						bStrictMode,
						OutFailReason))
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON object into %s property %s"),
						*PropertyClass->GetAuthoredName(),
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON object into {0} property {1}\n{2}"),
							FText::FromString(PropertyClass->GetAuthoredName()),
							FText::FromString(Property->GetAuthoredName()),
							*OutFailReason);
					}
					return false;
				}
			}
			else if (JsonValue->Type == EJson::String)
			{
				// Default to expect a string for everything else
				if (Property->ImportText_Direct(*JsonValue->AsString(), OutValue, nullptr, PPF_SerializedAsImportText)
					== nullptr)
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s"),
						*ObjectProperty->PropertyClass->GetAuthoredName(),
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON string into {0} property {1}"),
							FText::FromString(*ObjectProperty->PropertyClass->GetAuthoredName()),
							FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}

				// GRIMLORE Start jreich: Fixed hard refs to objects not resolving redirectors when loading
				while (auto* Redirector = Cast<UObjectRedirector>(ObjectProperty->GetObjectPropertyValue(OutValue)))
				{
					ObjectProperty->SetObjectPropertyValue(OutValue, Redirector->DestinationObject);
				}
				// GRIMLORE End
			}
		}
		// GRIMLORE Start dlehn: Fixed interface properties not resolving redirectors when loading
		else if (FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
		{
			if (JsonValue->Type == EJson::String)
			{
				const auto StringValue = JsonValue->AsString();
				const TCHAR* Buffer = *StringValue;
				UObject* TargetObject = nullptr;
				if (FObjectPropertyBase::ParseObjectPropertyValue(
						InterfaceProperty,
						nullptr,
						UObject::StaticClass(),
						PPF_SerializedAsImportText,
						Buffer,
						TargetObject)
					== false)
				{
					UE_LOG(
						LogJsonDataAsset,
						Error,
						TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s"),
						*InterfaceProperty->InterfaceClass->GetAuthoredName(),
						*Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(
							INVTEXT("Unable to import JSON string into {0} property {1}"),
							FText::FromString(*InterfaceProperty->InterfaceClass->GetAuthoredName()),
							FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}

				while (auto* Redirector = Cast<UObjectRedirector>(TargetObject))
				{
					TargetObject = Redirector->DestinationObject;
				}

				FScriptInterface LoadedValue;
				if (TargetObject)
				{
					if (TargetObject->GetClass()->ImplementsInterface(InterfaceProperty->InterfaceClass))
					{
						LoadedValue.SetObject(TargetObject);
						LoadedValue.SetInterface(TargetObject->GetInterfaceAddress(InterfaceProperty->InterfaceClass));
					}
					else
					{
						UE_LOG(
							LogJsonDataAsset,
							Error,
							TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s because "
								 "target object '%s' does not implement required interface."),
							*InterfaceProperty->InterfaceClass->GetAuthoredName(),
							*Property->GetAuthoredName(),
							*TargetObject->GetPathName());
						if (OutFailReason)
						{
							*OutFailReason = FText::Format(
								INVTEXT("Unable to import JSON string into {0} property {1} because target object '%s' "
										"does not implement required interface"),
								FText::FromString(InterfaceProperty->InterfaceClass->GetAuthoredName()),
								FText::FromString(Property->GetAuthoredName()),
								FText::FromString(TargetObject->GetPathName()));
						}

						// Don't return false here, that is not a fatal error.
					}
				}

				InterfaceProperty->SetPropertyValue(OutValue, LoadedValue);
			}
			else
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s because we expect "
						 "interface pointers to be serialized as a string."),
					*InterfaceProperty->InterfaceClass->GetAuthoredName(),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import JSON string into {0} property {1} because we expect interface "
								"pointers to be serialized as a string."),
						FText::FromString(*InterfaceProperty->InterfaceClass->GetAuthoredName()),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
		// GRIMLORE End
		else
		{
			// Default to expect a string for everything else
			if (Property->ImportText_Direct(*JsonValue->AsString(), OutValue, nullptr, PPF_SerializedAsImportText)
				== nullptr)
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Unable to import JSON string into property %s"),
					*Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import JSON string into property {0}"),
						FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}

		return true;
	}
};

bool FJsonLibraryImportHelper::JsonValueToFPropertyWithContainer(
	const TSharedPtr<FJsonValue>& JsonValue,
	FProperty* Property,
	void* OutValue,
	const UStruct* ContainerStruct,
	void* Container,
	const FArchive& VersionLoadingArchive,
	int64 CheckFlags,
	int64 SkipFlags,
	const bool bStrictMode,
	FText* OutFailReason)
{
	if (!JsonValue.IsValid())
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("JsonValueToUProperty - Invalid JSON value"));
		if (OutFailReason)
		{
			*OutFailReason = INVTEXT("Invalid JSON value");
		}
		return false;
	}

	const bool bArrayOrSetProperty = Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>();
	const bool bJsonArray = JsonValue->Type == EJson::Array;

	if (!bJsonArray)
	{
		if (bArrayOrSetProperty)
		{
			UE_LOG(LogJsonDataAsset, Error, TEXT("JsonValueToUProperty - Expecting JSON array"));
			if (OutFailReason)
			{
				*OutFailReason = INVTEXT("Expecting JSON array");
			}
			return false;
		}

		if (Property->ArrayDim != 1)
		{
			if (bStrictMode)
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonValueToUProperty - Property %s is not an array but has %d elements"),
					*Property->GetAuthoredName(),
					Property->ArrayDim);
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Property {0} is not an array but has {1} elements"),
						FText::FromString(Property->GetAuthoredName()),
						FText::AsNumber(Property->ArrayDim));
				}
				return false;
			}

			UE_LOG(
				LogJsonDataAsset,
				Warning,
				TEXT("Ignoring excess properties when deserializing %s"),
				*Property->GetAuthoredName());
		}

		return ConvertScalarJsonValueToFPropertyWithContainer(
			JsonValue,
			Property,
			OutValue,
			ContainerStruct,
			Container,
			VersionLoadingArchive,
			CheckFlags,
			SkipFlags,
			bStrictMode,
			OutFailReason);
	}

	// In practice, the ArrayDim == 1 check ought to be redundant, since nested arrays of FProperties are not
	// supported
	if (bArrayOrSetProperty && Property->ArrayDim == 1)
	{
		// Read into TArray
		return ConvertScalarJsonValueToFPropertyWithContainer(
			JsonValue,
			Property,
			OutValue,
			ContainerStruct,
			Container,
			VersionLoadingArchive,
			CheckFlags,
			SkipFlags,
			bStrictMode,
			OutFailReason);
	}

	// We're deserializing a JSON array
	const auto& ArrayValue = JsonValue->AsArray();

	// GRIMLORE Start dlehn: Manually handle import for gameplay tag containers because the unreal ones don't
	// properly implement it.
	if (const auto StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct->IsChildOf(FGameplayTagContainer::StaticStruct()))
		{
			auto& TagContainer = *static_cast<FGameplayTagContainer*>(OutValue);
			TagContainer.Reset();
			const auto& JsonArray = JsonValue->AsArray();
			for (const auto& JsonTagValue : JsonArray)
			{
				FGameplayTag Tag;
				if (UGameplayTagsManager::Get().ImportSingleGameplayTag(Tag, FName(*JsonTagValue->AsString()), true)
					&& Tag.IsValid())
				{
					TagContainer.AddTag(Tag);
				}
			}
			return true;
		}
	}
	// GRIMLORE End

	if (bStrictMode && (Property->ArrayDim != ArrayValue.Num()))
	{
		UE_LOG(
			LogJsonDataAsset,
			Error,
			TEXT("JsonValueToUProperty - JSON array size is incorrect (has %d elements, but needs %d)"),
			ArrayValue.Num(),
			Property->ArrayDim);
		if (OutFailReason)
		{
			*OutFailReason = FText::Format(
				INVTEXT("JSON array size is incorrect (has {0} elements, but needs {1})"),
				FText::AsNumber(ArrayValue.Num()),
				FText::AsNumber(Property->ArrayDim));
		}
		return false;
	}

	if (Property->ArrayDim < ArrayValue.Num())
	{
		UE_LOG(
			LogJsonDataAsset,
			Warning,
			TEXT("Ignoring excess properties when deserializing %s"),
			*Property->GetAuthoredName());
	}

	// Read into native array
	const int32 ItemsToRead = FMath::Clamp(ArrayValue.Num(), 0, Property->ArrayDim);
	for (int Index = 0; Index != ItemsToRead; ++Index)
	{
		if (!ConvertScalarJsonValueToFPropertyWithContainer(
				ArrayValue[Index],
				Property,
				StaticCast<char*>(OutValue) + Index * Property->ElementSize,
				ContainerStruct,
				Container,
				VersionLoadingArchive,
				CheckFlags,
				SkipFlags,
				bStrictMode,
				OutFailReason))
		{
			return false;
		}
	}
	return true;
}

bool FJsonLibraryImportHelper::JsonAttributesToUStructWithContainer(
	const TMap<FString, TSharedPtr<FJsonValue>>& JsonAttributes,
	const UStruct* StructDefinition,
	void* OutStruct,
	const UStruct* ContainerStruct,
	void* Container,
	const FArchive& VersionLoadingArchive,
	int64 CheckFlags,
	int64 SkipFlags,
	const bool bStrictMode,
	FText* OutFailReason)
{
	DECLARE_CYCLE_STAT(
		TEXT("JsonAttributesToUStructWithContainer"),
		STAT_JsonAttributesToUStructWithContainer,
		STATGROUP_OUUJsonData);

	if (StructDefinition == FJsonObjectWrapper::StaticStruct())
	{
		// Just copy it into the object
		FJsonObjectWrapper* ProxyObject = StaticCast<FJsonObjectWrapper*>(OutStruct);
		ProxyObject->JsonObject = MakeShared<FJsonObject>();
		ProxyObject->JsonObject->Values = JsonAttributes;
		return true;
	}

	int32 NumUnclaimedProperties = JsonAttributes.Num();
	if (NumUnclaimedProperties <= 0)
	{
		return true;
	}

	// iterate over the struct properties
	for (TFieldIterator<FProperty> PropIt(StructDefinition); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Check to see if we should ignore this property
		if (CheckFlags != 0 && !Property->HasAnyPropertyFlags(CheckFlags))
		{
			continue;
		}
		if (Property->HasAnyPropertyFlags(SkipFlags))
		{
			continue;
		}

		// find a JSON value matching this property name
		FString PropertyName = StructDefinition->GetAuthoredNameForField(Property);
		const TSharedPtr<FJsonValue>* JsonValue = JsonAttributes.Find(PropertyName);

		if (!JsonValue)
		{
			if (bStrictMode)
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonObjectToUStruct - Missing JSON value named %s"),
					*PropertyName);
				if (OutFailReason)
				{
					*OutFailReason =
						FText::Format(INVTEXT("Missing JSON value named {0}"), FText::FromString(PropertyName));
				}
				return false;
			}

			// we allow values to not be found since this mirrors the typical UObject mantra that all the fields are
			// optional when deserializing
			continue;
		}

		if (JsonValue->IsValid() && !(*JsonValue)->IsNull())
		{
			void* Value = Property->ContainerPtrToValuePtr<uint8>(OutStruct);
			if (!JsonValueToFPropertyWithContainer(
					*JsonValue,
					Property,
					Value,
					ContainerStruct,
					Container,
					VersionLoadingArchive,
					CheckFlags,
					SkipFlags,
					bStrictMode,
					OutFailReason))
			{
				UE_LOG(
					LogJsonDataAsset,
					Error,
					TEXT("JsonObjectToUStruct - Unable to import JSON value into property %s"),
					*PropertyName);
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(
						INVTEXT("Unable to import JSON value into property {0}\n{1}"),
						FText::FromString(PropertyName),
						*OutFailReason);
				}
				return false;
			}
		}

		if (--NumUnclaimedProperties <= 0)
		{
			// Should we log a warning/error if we still have properties in the JSON data that aren't in the struct
			// definition in strict mode?

			// If we found all properties that were in the JsonAttributes map, there is no reason to keep looking
			// for more.
			break;
		}
	}

	// GRIMLORE Start dlehn: Ensure objects loaded from json receive PostLoad calls
	if (StructDefinition->IsChildOf<UObject>())
	{
		const auto pObject = StaticCast<UObject*>(OutStruct);
		if (pObject->HasAnyFlags(RF_NeedPostLoad) == false)
		{
			pObject->SetFlags(RF_NeedPostLoad);
			pObject->ConditionalPostLoad();
		}
	}
	else if (const auto ScriptStruct = Cast<UScriptStruct>(StructDefinition))
	{
		if (const auto StructOps = ScriptStruct->GetCppStructOps())
		{
			if (StructOps->HasPostSerialize())
			{
				StructOps->PostSerialize(VersionLoadingArchive, OutStruct);
			}
		}
	}

	// Fix for gameplay tag container's ImportTextItem function not being called because they have no matching
	// ExportTextItem function. So we have to manually do what they would otherwise do during import.
	if (StructDefinition->IsChildOf(FGameplayTagContainer::StaticStruct()))
	{
		auto& TagContainer = *StaticCast<FGameplayTagContainer*>(OutStruct);
		// Remove invalid tags. Unfortunately there is no public function to remove all invalid tags at once.
		while (TagContainer.RemoveTag(FGameplayTag(), true))
		{
		}
		TagContainer.FillParentTags();
	}

	// GRIMLORE End

	return true;
}

TSharedPtr<FJsonObject> UOUUJsonLibrary::UStructToJsonObject(
	const void* Data,
	const void* DefaultData,
	const UStruct* Struct,
	const FOUUJsonLibraryObjectFilter& SubObjectFilter,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */,
	bool bOnlyModifiedProperties /* = false */)
{
	if (!Data || !IsValid(Struct))
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to convert invalid struct TO Json object"));
		return nullptr;
	}

	const FJsonLibraryExportHelper Helper{CheckFlags, SkipFlags, SubObjectFilter, bOnlyModifiedProperties};
	return Helper.ConvertStructToJsonObject(Data, DefaultData, Struct);
}

TSharedPtr<FJsonObject> UOUUJsonLibrary::UObjectToJsonObject(
	const UObject* Object,
	FOUUJsonLibraryObjectFilter SubObjectFilter,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */,
	bool bOnlyModifiedProperties /* = false */)
{
	if (!IsValid(Object))
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to convert invalid object TO Json object"));
		return nullptr;
	}

	const FJsonLibraryExportHelper Helper{CheckFlags, SkipFlags, SubObjectFilter, bOnlyModifiedProperties};
	return Helper.ConvertObjectToJsonObject(Object);
}

TSharedPtr<FJsonValue> UOUUJsonLibrary::UPropertyToJsonValue(
	const void* PropertyData,
	const void* DefaultPropertyData,
	FProperty* Property,
	const FOUUJsonLibraryObjectFilter& SubObjectFilter,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */,
	bool bOnlyModifiedProperties /* = false */)
{
	if (!PropertyData || !Property)
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to convert invalid property TO Json value"));
		return nullptr;
	}

	const FJsonLibraryExportHelper Helper{CheckFlags, SkipFlags, SubObjectFilter, bOnlyModifiedProperties};
	return Helper.ConvertPropertyToJsonValue(PropertyData, DefaultPropertyData, Property);
}

bool UOUUJsonLibrary::JsonValueToUProperty(
	const TSharedRef<FJsonValue>& JsonValue,
	void* PropertyData,
	FProperty* Property,
	const FArchive& VersionLoadingArchive,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */)
{
	FJsonLibraryImportHelper Helper;
	return Helper.JsonValueToUProperty(JsonValue, Property, PropertyData, VersionLoadingArchive, CheckFlags, SkipFlags);
}

FString UOUUJsonLibrary::UObjectToJsonString(
	const UObject* Object,
	FOUUJsonLibraryObjectFilter SubObjectFilter,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */,
	bool bOnlyModifiedProperties /* = false */)
{
	if (!IsValid(Object))
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to convert invalid object TO Json string"));
		return OUU::JsonData::Runtime::Private::InvalidConversionResultString;
	}

	FJsonLibraryExportHelper Helper{CheckFlags, SkipFlags, SubObjectFilter, bOnlyModifiedProperties};

	constexpr bool bPrettyPrint = true;
	return Helper.ConvertObjectToString<bPrettyPrint>(Object);
}

bool UOUUJsonLibrary::JsonStringToUObject(
	UObject* Object,
	FString String,
	const FJsonDataCustomVersions& CustomVersions,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */)
{
	FArchive VersionLoadingArchive;
	VersionLoadingArchive.SetIsLoading(true);
	VersionLoadingArchive.SetIsPersistent(true);
	VersionLoadingArchive.SetCustomVersions(CustomVersions.ToCustomVersionContainer());

	return JsonStringToUObject(Object, String, VersionLoadingArchive, CheckFlags, SkipFlags);
}

bool UOUUJsonLibrary::JsonStringToUObject(
	UObject* Object,
	FString String,
	const FArchive& VersionLoadingArchive,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */)
{
	if (!IsValid(Object))
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to convert invalid object FROM Json string"));
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(String);
	if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogJsonDataAsset, Warning, TEXT("JsonStringToUObject - Unable to parse json=[%s]"), *String);
		return false;
	}

	FJsonLibraryImportHelper Helper;
	if (!Helper.JsonObjectToUStruct(
			JsonObject.ToSharedRef(),
			Object->GetClass(),
			Object,
			VersionLoadingArchive,
			CheckFlags,
			SkipFlags))
	{
		UE_LOG(LogJsonDataAsset, Warning, TEXT("JsonStringToUObject - Unable to deserialize. json=[%s]"), *String);
		return false;
	}
	return true;
}

bool UOUUJsonLibrary::JsonObjectToUStruct(
	const TSharedRef<FJsonObject>& JsonObject,
	const UStruct* StructDefinition,
	void* OutStruct,
	const FArchive& VersionLoadingArchive,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */)
{
	FJsonLibraryImportHelper Helper;
	if (!Helper.JsonObjectToUStruct(
			JsonObject,
			StructDefinition,
			OutStruct,
			VersionLoadingArchive,
			CheckFlags,
			SkipFlags))
	{
		UE_LOG(LogJsonDataAsset, Warning, TEXT("JsonObjectToUStruct - Unable to deserialize json object."));
		return false;
	}
	return true;
}

bool UOUUJsonLibrary::JsonObjectToUObject(
	const TSharedRef<FJsonObject>& JsonObject,
	UObject* OutObject,
	const FArchive& VersionLoadingArchive,
	int64 CheckFlags /* = 0 */,
	int64 SkipFlags /* = 0 */)
{
	if (!IsValid(OutObject))
	{
		UE_LOG(LogJsonDataAsset, Error, TEXT("Failed to convert invalid object FROM Json string"));
		return false;
	}

	return JsonObjectToUStruct(
		JsonObject,
		OutObject->GetClass(),
		OutObject,
		VersionLoadingArchive,
		CheckFlags,
		SkipFlags);
}
