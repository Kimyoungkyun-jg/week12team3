#pragma once

#include "Asset/FbxImporter/FbxPath.h"

// 시퀀스 이름은 파일 이름(AS_<FBX 이름>[_<스택 이름>])에서 확장자를 뺀 것이다. 파일 이름이 이미 FBX와 스택을 구분한다.
inline FString GetAnimSequenceDisplayName(const FString& AssetPath, const FString& /*SkeletonPath*/)
{
	return FbxPath::ToString(FbxPath::FromString(AssetPath).stem());
}
