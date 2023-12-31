// Copyright (c) 2023 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "Stats/Stats2.h"

// ReSharper disable CppUnusedIncludeDirective
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
// ReSharper restore CppUnusedIncludeDirective

OUUJSONDATARUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogJsonDataAsset, Log, All);

#define JSON_DATA_MESSAGELOG_CATEGORY "AssetTools"

#define UE_JSON_DATA_MESSAGELOG(Severity, Obj, FmtText, ...)                                                           \
	FMessageLog(JSON_DATA_MESSAGELOG_CATEGORY)                                                                         \
		.AddMessage(FTokenizedMessage::Create(EMessageSeverity::Severity)                                              \
						->AddToken(FUObjectToken::Create(Obj))                                                         \
						->AddToken(FTextToken::Create(FText::FromString(FString::Printf(FmtText, ##__VA_ARGS__)))));

DECLARE_STATS_GROUP(TEXT("OUUJsonData"), STATGROUP_OUUJsonData, STATCAT_Advanced);
