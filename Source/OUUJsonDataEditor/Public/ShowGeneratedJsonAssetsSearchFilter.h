// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "ContentBrowserFrontEndFilterExtension.h"

#include "ShowGeneratedJsonAssetsSearchFilter.generated.h"

UCLASS()
class UShowGeneratedJsonAssetsSearchFilter : public UContentBrowserFrontEndFilterExtension
{
public:
	GENERATED_BODY()

	// - UContentBrowserFrontEndFilterExtension
	void AddFrontEndFilterExtensions(
		TSharedPtr<class FFrontendFilterCategory> DefaultCategory,
		TArray<TSharedRef<class FFrontendFilter>>& InOutFilterList) const override;
	// --
};
