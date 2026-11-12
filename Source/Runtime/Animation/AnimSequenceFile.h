#pragma once

#include "Core/EngineString.h"
#include <filesystem>

class UAnimSequence;

namespace AnimSequenceFile
{
	bool Read(const std::filesystem::path& Path, UAnimSequence& Sequence, FString& OutError);
	bool Write(const std::filesystem::path& Path, UAnimSequence& Sequence, FString& OutError);
	bool IsAnimSequencePath(const FString& Path);
}
