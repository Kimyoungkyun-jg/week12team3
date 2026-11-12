#pragma once

#include "Core/EngineString.h"

#include <filesystem>

class UBlendSpace1D;

namespace BlendSpaceFile
{
    bool Read(const std::filesystem::path& Path, UBlendSpace1D& BlendSpace, FString& OutError);
    bool Write(const std::filesystem::path& Path, UBlendSpace1D& BlendSpace, FString& OutError);
    bool IsBlendSpacePath(const FString& Path);
}