#pragma once

#include "Core/EngineString.h"

#include <filesystem>
#include <functional>

class FBlendSpaceAssetCreator
{
public:
    void DrawContextMenu(const FString& AssetPath, const std::filesystem::path& Directory);
    void DrawMenuItems(const FString& AssetPath, const std::filesystem::path& Directory);
    void ProcessCreateRequest();
    void SetCreatedCallback(std::function<void(const FString&)> InCallback)
    {
        OnCreated = std::move(InCallback);
    }

private:
    bool CreateAsset(const std::filesystem::path& Target);

    std::function<void(const FString&)> OnCreated;

    FString PreviewMeshPath;
    std::filesystem::path CreateDirectory;
    bool bOpenDialog = false;
};
