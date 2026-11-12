#pragma once

#include "AnimBlueprintData.h"
#include <filesystem>

namespace AnimBlueprintFile
{
    bool IsAnimBlueprintPath(const FString& Path);
    FAnimBlueprintData CreateDefault(const FString& PreviewMeshPath);
    bool Validate(const FAnimBlueprintData& Data, FString& OutError);
    bool ToJson(const FAnimBlueprintData& Data, FString& OutText, FString& OutError, bool bPretty = false);
    bool Read(const std::filesystem::path& Path, FAnimBlueprintData& OutData, FString& OutError);
    bool Write(const std::filesystem::path& Path, const FAnimBlueprintData& Data, FString& OutError);
}
