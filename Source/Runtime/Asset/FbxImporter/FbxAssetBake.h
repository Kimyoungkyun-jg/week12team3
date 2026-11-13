#pragma once
#include "FbxAssetData.h"

class FFbxAssetBake
{
public:
	static bool ReadBaked(const FString& Path, FFbxAssetData& Out, FString& Error);
	static bool WriteBaked(const FString& Path, const FFbxAssetData& Data, FString& Error);
	// 재임포트 이름 판정은 페이로드 버전과 독립적인 공통 출처 필드만 읽는다
	static bool ReadSourceIdentity(const FString& Path, FString& Source, FString& Object);
	// 페이로드를 읽지 않고 타입, 출처, 해시, 옵션만 채운다
	static bool ReadHeader(const FString& Path, FFbxAssetData& Out, FString& Error);
	// 예전 .animseq.bin의 루트 모션 설정만 읽는다. 첫 임포트 때 설정을 옮기는 데 쓴다.
	static bool ReadLegacyAnimationSettings(const FString& Path, FFbxAnimationData& InOut);
};
