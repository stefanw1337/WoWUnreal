#pragma once
#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

// Milestones completed by the native client, never elapsed-time estimates.
inline void WowReportStartupProgress(int32 Completed)
{
    FString Ready;
    if (!FParse::Value(FCommandLine::Get(), TEXT("-splashreadyfile="), Ready) || Ready.IsEmpty()) return;
    const FString Progress = Ready + TEXT(".progress");
    const FString Temporary = Progress + TEXT(".tmp");
    if (FFileHelper::SaveStringToFile(FString::FromInt(Completed), *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        IFileManager::Get().Move(*Progress, *Temporary, true);
}
