#pragma once

#include "Core/EngineString.h"
#include <filesystem>
#include <functional>

class FAnimBlueprintAssetCreator
{
public:
    void DrawMenuItems(const FString& AssetPath, const std::filesystem::path& Directory);
    void ProcessCreateRequest();
    void SetCreatedCallback(std::function<void(const FString&)> Callback);

private:
    FString PreviewMeshPath;
    std::filesystem::path CreateDirectory;
    std::function<void(const FString&)> OnCreated;
    bool bCreateRequested = false;
};
