// Copyright (c) 2023 Jonas Reich & Contributors

#include "JsonDataAssetLibrary.h"

UJsonDataAsset* UJsonDataAssetLibrary::ResolveObjectJsonDataAsset(const FJsonDataAssetPath& Path)
{
	return Path.ResolveObject();
}

UJsonDataAsset* UJsonDataAssetLibrary::LoadJsonDataAssetSynchronous(const FJsonDataAssetPath& Path)
{
	return Path.LoadSynchronous();
}

UJsonDataAsset* UJsonDataAssetLibrary::ForceReloadJsonDataAsset(const FJsonDataAssetPath& Path)
{
	return Path.ForceReload();
}

UJsonDataAsset* UJsonDataAssetLibrary::GetSoftJsonDataAssetPtr(
	const FSoftJsonDataAssetPtr& Ptr,
	TSubclassOf<UJsonDataAsset> Class)
{
	const auto Object = Ptr.Get();
	if (Object && Class && Object->IsA(Class) == false)
	{
		return nullptr;
	}

	return Object;
}

UJsonDataAsset* UJsonDataAssetLibrary::LoadJsonDataAssetPtrSyncronous(
	const FSoftJsonDataAssetPtr& Ptr,
	TSubclassOf<UJsonDataAsset> Class)
{
	const auto Object = Ptr.LoadSynchronous();
	if (Object && Class && Object->IsA(Class) == false)
	{
		return nullptr;
	}

	return Object;
}

UJsonDataAsset* UJsonDataAssetLibrary::GetJsonDataAssetPtr(
	const FJsonDataAssetPtr& Ptr,
	TSubclassOf<UJsonDataAsset> Class)
{
	const auto Object = Ptr.Get();
	if (Object && Class && Object->IsA(Class) == false)
	{
		return nullptr;
	}

	return Object;
}

UJsonDataAsset* UJsonDataAssetLibrary::Conv_SoftJsonDataAssetPtrToRawPtr(const FSoftJsonDataAssetPtr& InPtr)
{
	return InPtr.Get();
}

FSoftJsonDataAssetPtr UJsonDataAssetLibrary::Conv_RawPtrToSoftJsonDataAssetPtr(UJsonDataAsset* InPtr)
{
	return FSoftJsonDataAssetPtr(InPtr);
}
UJsonDataAsset* UJsonDataAssetLibrary::Conv_JsonDataAssetPtrToRawPtr(const FJsonDataAssetPtr& InPtr)
{
	return InPtr.Get();
}
FJsonDataAssetPtr UJsonDataAssetLibrary::Conv_RawPtrToJsonDataAssetPtr(UJsonDataAsset* InPtr)
{
	return FJsonDataAssetPtr(InPtr);
}
