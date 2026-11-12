#pragma once

#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"

enum class EFbxImportMode : uint32 { Static, Skeletal };
enum class EFbxMeshKind : uint32 { Static = 1, Skeletal = 2 };

// 원본 비교는 명시적 Import에서만 사용한다. Binary 로딩은 원본 파일에 접근하지 않는다.
struct FFbxSourceInfo
{
	static constexpr uint32 AnimationChecked = 1u << 2;
	static constexpr uint32 AnimationOutput = 1u << 3;
	FString Path;
	int64 ModifiedTime = 0;
	uint64 Size = 0;
	EFbxImportMode Mode = EFbxImportMode::Static;
	uint32 Outputs = 0; // EFbxMeshKind 비트와 애니메이션 출력 상태
};

struct FFbxBakedMesh
{
	EFbxMeshKind Kind = EFbxMeshKind::Static;
	FFbxSourceInfo Source;
	FStaticMeshData StaticMesh;
	FSkeletalMeshData SkeletalMesh;
};

// SDK와 독립적인 엔진 자료만 읽고 쓴다. 형식/필드 또는 구운 결과의 의미가 바뀌면 Version을 올린다.
class FFbxMeshBake
{
public:
	static bool IsBakedPath(const FString& Path);
	static bool Read(const FString& Path, FFbxBakedMesh& OutData, FString& OutError);
	static bool Write(const FString& Path, const FFbxBakedMesh& Data, FString& OutError);
};
