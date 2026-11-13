#pragma once
#include "Core/EngineString.h"
#include "Core/Types.h"
#include <filesystem>

namespace FbxPath
{
	FString ToString(const std::filesystem::path& Path);
	inline std::filesystem::path FromString(const FString& Value)
	{
		return std::filesystem::path(std::u8string(Value.begin(), Value.end()));
	}
	bool GetAssetKey(const FString& Path, FString& Key, FString& Error, bool bMustExist = true);
	// 원본은 Assets 밖에서도 읽는다. Identity는 출처 비교용이며 런타임 애셋 키가 아니다.
	bool GetSourcePath(const FString& Input, std::filesystem::path& Path, FString& Identity, FString& Error);
	FString DefaultOutputDirectory(const FString& Source);
	bool GetOutputDirectory(const FString& Input, std::filesystem::path& Path, FString& Error);
	bool ResolveKey(const FString& Key, FString& Path, FString& Error);
	bool ResolveKey(const FString& Key, std::filesystem::path& Path, FString& Error);
	uint64 StableHash(const FString& Value);
	// 파일 내용 해시. 읽지 못하면 0이다.
	uint64 FileHash(const std::filesystem::path& Path);
	// 바이너리에 저장된 출처 식별자를 실제 경로로 바꾼다. Assets 상대 경로와 외부 절대 경로를 모두 받는다.
	std::filesystem::path SourceFile(const FString& Identity);
	FString FileName(const FString& Value);
}
