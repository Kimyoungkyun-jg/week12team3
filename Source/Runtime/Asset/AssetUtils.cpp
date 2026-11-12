#include "EnginePCH.h"
#include "Asset/AssetUtils.h"

#include <algorithm>
#include <filesystem>

std::filesystem::path ToPath(const FString& Value)
{
	return std::filesystem::path(std::u8string(Value.begin(), Value.end()));
}

FString ToString(const std::filesystem::path& Path)
{
	const auto UTF8 = Path.generic_u8string();
	return FString(UTF8.begin(), UTF8.end());
}

bool IsBuiltinKey(const std::filesystem::path& Path)
{
	return !Path.has_parent_path() && !Path.has_extension();
}

bool HasAssetsPrefix(const std::filesystem::path& Path)
{
	if (Path.empty())
	{
		return false;
	}

	FString First = ToString(*Path.begin());
	for (char& Character : First)
	{
		if (Character >= 'A' && Character <= 'Z')
		{
			Character += 'a' - 'A';
		}
	}

	return First == "assets";
}

FString FAssetUtils::NormalizeAssetPath(const FString& Path)
{
	if (Path.empty())
	{
		return "";
	}

	// '\' 문자를 '/' 문자로 교체
	FString Normalized = Path;
	std::replace(Normalized.begin(), Normalized.end(), '\\', '/');

	// 상대 경로들 resolve
	const auto Result = ToPath(Normalized).lexically_normal();
	return Result == "." ? FString{} : ToString(Result);
}

FString FAssetUtils::ResolveAssetFilePath(const FString& Path)
{
	const FString Normalized = NormalizeAssetPath(Path);
	if (Normalized.empty())
	{
		return "";
	}

	const auto Input = ToPath(Normalized);
	if (IsBuiltinKey(Input))
	{
		return "";
	}

	std::filesystem::path FilePath = Input;
	if (Input.is_relative())
	{
		// Assets 접두 경로와 상위 디렉터리 경로는 프로젝트 기준으로 해석한다.
		// 나머지는 Assets 상대 키로 해석하며, 현재 파일 유무로 추측하지 않는다.
		if (!HasAssetsPrefix(Input) && *Input.begin() != "..")
		{
			FilePath = std::filesystem::path("Assets") / Input;
		}
	}

	std::error_code Error;
	const auto Absolute = std::filesystem::absolute(FilePath, Error);
	return Error ? FString{} : ToString(Absolute.lexically_normal());
}

FString FAssetUtils::GetAssetKey(const FString& Path)
{
	const FString Normalized = NormalizeAssetPath(Path);
	if (Normalized.empty())
	{
		return "";
	}

	if (IsBuiltinKey(ToPath(Normalized)))
	{
		return Normalized;
	}

	const FString FilePath = ResolveAssetFilePath(Normalized);
	if (FilePath.empty())
	{
		return "";
	}

	std::error_code Error;
	const auto Root = std::filesystem::absolute("Assets", Error).lexically_normal();
	if (Error)
	{
		return "";
	}

	const auto Absolute = ToPath(FilePath);
	auto FilePart = Absolute.begin();
	for (const auto& RootPart : Root)
	{
		if (FilePart == Absolute.end())
		{
			return FilePath;
		}

		const std::wstring FilePathWStr = FilePart->generic_wstring();
		const std::wstring RootPathWStr = RootPart.generic_wstring();

		// Windows의 경로 비교 규칙을 사용하되 파일명의 대소문자 표기는 보존한다.
		if (_wcsicmp(FilePathWStr.c_str(), RootPathWStr.c_str()) != 0)
		{
			return FilePath;
		}
		
		++FilePart;
	}

	std::filesystem::path Relative;
	for (; FilePart != Absolute.end(); ++FilePart)
	{
		Relative /= *FilePart;
	}

	return ToString(Relative);
}
