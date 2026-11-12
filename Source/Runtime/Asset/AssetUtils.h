#pragma once

#include "Core/EngineString.h"

class FAssetUtils
{
public:
	FAssetUtils() = delete;

	// 구분자를 /로 통일하고 .과 ..을 정리한다. 파일 존재 여부는 검사하지 않는다.
	static FString NormalizeAssetPath(const FString& Path);

	// Assets 내부 파일은 Assets 상대 키, 외부 파일은 절대 경로를 반환한다.
	// 확장자와 디렉터리가 없는 Cube 등의 내장 키는 그대로 유지한다.
	static FString GetAssetKey(const FString& Path);

	// Assets 상대 키 또는 파일 경로를 절대 파일 경로로 해석한다.
	// 내장 키 및 빈 입력은 파일이 없으므로 빈 문자열을 반환한다.
	static FString ResolveAssetFilePath(const FString& Path);
};
