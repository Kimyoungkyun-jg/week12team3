#include "EnginePCH.h"

#include "Animation/BlendSpaceFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

#include "Animation/BlendSpace1D.h"
#include "Asset/AssetUtils.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

bool BlendSpaceFile::Read(const std::filesystem::path& Path, UBlendSpace1D& BlendSpace, FString& OutError)
{
    OutError.clear();
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    const auto Size = static_cast<std::streamoff>(File.tellg());

    if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
    {
        OutError = "BlendSpace: cannot open file or invalid size";
        return false;
    }

    TArray<uint8> Bytes;
    Bytes.SetNum(static_cast<int32>(Size));
    File.seekg(0);
    if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
    {
        OutError = "BlendSpace: cannot read file";
        return false;
    }

    FMemoryReader Reader(Bytes, true);
    
    FJsonArchiveInputFormatter Formatter(Reader);
    if (Reader.IsError())
    {
        OutError = "BlendSpace: invalid JSON";
        return false;
    }
    FStructuredArchive Archive(Formatter);
    BlendSpace.Serialize(Archive.Open().EnterRecord());
    

    BlendSpace.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
    return true;
}

bool BlendSpaceFile::Write(const std::filesystem::path& Path, UBlendSpace1D& BlendSpace, FString& OutError)
{
    OutError.clear();
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    {
        FJsonArchiveOutputFormatter Formatter(Writer);
        FStructuredArchive Archive(Formatter);
        BlendSpace.Serialize(Archive.Open().EnterRecord());
    }

    if (Writer.IsError())
    {
        OutError = "BlendSpace: cannot serialize data";
        return false;
    }

    std::ofstream File(Path, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        OutError = "BlendSpace: cannot open output file";
        return false;
    }

    File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
    File.close();
    if (!File)
    {
        OutError = "BlendSpace: cannot write file";
        return false;
    }
    BlendSpace.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
    return true;
}

bool BlendSpaceFile::IsBlendSpacePath(const FString& Path)
{
    FString Extension = std::filesystem::path(Path).extension().string();

    std::transform(Extension.begin(), Extension.end(), Extension.begin(),
        [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

    return Extension == ".blendspace";
}