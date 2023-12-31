// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "EditorSubsystem.h"

#include "OUUJsonDataEditorSubsystem.generated.h"

UCLASS()
class OUUJSONDATAEDITOR_API UOUUJsonDataEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// - USubsystem
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;
	// --
};
