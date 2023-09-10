// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"

OUUJSONDATARUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogJsonDataAsset, Log, All);

#define JSON_DATA_MESSAGELOG_CATEGORY "AssetTools"

#define UE_JSON_DATA_MESSAGELOG(Severity, Obj, FmtText, ...)                                                           \
	FMessageLog(JSON_DATA_MESSAGELOG_CATEGORY)                                                                         \
		.AddMessage(FTokenizedMessage::Create(EMessageSeverity::Severity)                                              \
						->AddToken(FUObjectToken::Create(Obj))                                                         \
						->AddToken(FTextToken::Create(FText::FromString(FString::Printf(FmtText, ##__VA_ARGS__)))));
