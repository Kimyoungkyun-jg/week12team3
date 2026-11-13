#pragma once
#include "FbxAssetData.h"

class URenderAsset;

// 소스 해석은 FFbxAssetImporter, 바이너리/런타임 애셋 생성과 교체는 이 로더가 담당한다.
class FFbxAssetLoader
{
public:
	static URenderAsset* LoadAsset(const FString& Path, FString& Error);
	// OwnerSource를 주면 SourcePath를 읽어 OwnerSource의 바이너리를 덮어쓴다 (Reimport From File).
	static FFbxImportResult Import(const FString& SourcePath, const FFbxImportOptions& Options, const FString& OutputDirectory = {}, const FString& OwnerSource = {});
	// 재임포트로 교체된 Skeleton을 가리키는 애니메이션 애셋을 새 Skeleton으로 옮긴다.
	static void RebindToReimportedSkeletons();
private:
	static URenderAsset* CreateTexture(const FImageData& Image, FString& Error);
};
