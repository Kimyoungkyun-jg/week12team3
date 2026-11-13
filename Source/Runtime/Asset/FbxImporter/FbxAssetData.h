#pragma once

#include "Animation/ReferenceSkeleton.h"
#include "Asset/AnimNotifyEvent.h"
#include "Asset/AnimDataModel.h"
#include "Render/SkeletalMesh.h"
#include "Render/StaticMeshData.h"
#include "Render/ImageLoader.h"
#include "Render/RenderEnums.h"
#include "Render/RenderStates.h"
#include <array>

struct FSkinWeightData
{
	TArray<std::array<uint16, 4>> InfluenceBones;
	TArray<std::array<float, 4>> InfluenceWeights;
	bool Validate(int32 VertexCount, int32 BoneCount, FString& Error) const;
};

enum class EFbxAssetType : uint32
{
	StaticMesh = 1,
	SkeletalMesh,
	Skeleton,
	Material,
	Texture2D,
	AnimSequence,

	// 새 타입은 위에 추가하고 Last를 옮긴다. .fbxbin 헤더 검사가 이 범위를 쓴다.
	First = StaticMesh,
	Last = AnimSequence,
};

struct FFbxImportOptions
{
	bool bBakeStaticMeshes = true;
	bool bCombineStaticMeshes = false;
	bool bBakeSkeletalMeshes = true;
	bool bCombineSkeletalMeshes = true;
	// 둘 다 켜면 스킨 유무로 나누되, 스킨 없이 Transform 키로 움직이는 메시는 노드를 본으로 삼아 스켈레탈로 만든다.
	// Static만 켜면 스킨을 무시하고, Skeletal만 켜면 스킨 없는 메시도 노드를 본으로 삼아 스켈레탈로 만든다.
	bool bImportStaticMeshes = true;
	bool bImportSkeletalMeshes = true;
	// 스켈레탈 메시의 리그마다 AnimStack 하나를 AnimSequence 하나로 만든다. 메시가 없는 FBX는 TargetSkeletonKey를 지정해야 받는다.
	bool bImportAnimations = true;
	// 비어 있지 않으면 이 Skeleton(애셋 키)에 맞춰 애니메이션만 만든다. 메시와 새 Skeleton은 만들지 않고, 메시 없는 FBX도 받는다.
	// 임포트 요청에만 쓰이며 .fbxbin에는 저장하지 않는다. AnimSequence는 SkeletonKey로 대상을 기억한다.
	FString TargetSkeletonKey;
	// Target Skeleton과 맞아야 하는 본의 비율(FBX 리그와 대상 중 작은 쪽 기준). 0이면 하나만 맞아도 받는다.
	// 사용자가 고른 대상은 0, 임포트 창의 자동 대상 선택은 높은 값을 써서 다른 캐릭터의 Skeleton에 붙지 않게 한다. 저장하지 않는다.
	float MinTargetBoneMatch = 0.f;
	bool bForceFrontX = true;
	float UniformScale = 1.f; // 정점과 본 이동에 곱한다.
	// 재임포트할 때 기존 AnimSequence의 Notify를 유지한다. 끄면 지운다. 임포트 요청에만 쓰이며 .fbxbin에는 저장하지 않는다.
	bool bKeepNotifies = true;
};

struct FFbxMaterialData
{
	FVector4 DiffuseOrFactor{ 1, 1, 1, 1 }; // 선형 RGB: Diffuse 맵이 없으면 Diffuse × DiffuseFactor, 있으면 DiffuseFactor를 각 채널에 저장한다. W는 불투명도.
	float Shininess = 32.f;
	FVector SpecularColor = FVector(1, 1, 1); // 선형 반사광 색상
	float SpecularFactor = 1.f; // 반사광 강도 계수
	FVector EmissiveColor = FVector(0, 0, 0); // 선형 자체 발광 색상
	float EmissiveFactor = 1.f; // 자체 발광 강도 계수
	ELightingModel ShadingModel = ELightingModel::Phong;
	ESamplerState SamplerState = ESamplerState::LinearClamp;
	EBlendState BlendState = EBlendState::Opaque;
	FString DiffuseTexture;
	FString NormalTexture;
};

// 본 하나의 키. 이름은 FName이 아니라 원래 문자열로 둔다. FName은 "_숫자" 접미사를 번호로 떼어 내 "spine_01"이 "spine_1"로 바뀌므로
// 파일에 저장하면 원래 본 이름과 맞지 않게 된다.
struct FFbxBoneTrack
{
	FString BoneName;
	FRawAnimSequenceTrack InternalTrack;
};

// AnimStack 하나를 프레임마다 샘플링한 결과. 로더가 UAnimSequence + UAnimDataModel로 옮긴다.
// 트랙은 본 이름으로 연결하고, 대상 Skeleton에 없는 본은 빠질 수 있다. 성분마다 키는 NumberOfKeys개이거나 상수면 1개다.
// 루트 모션 설정은 에디터에서 바꿔 저장하는 값이다. 재임포트할 때 기존 파일의 값을 유지한다.
struct FFbxAnimationData
{
	FString Name; // AnimStack 이름
	FAnimFrameRate FrameRate;
	int32 NumberOfKeys = 0;
	TArray<FFbxBoneTrack> Tracks;
	int32 SourceTakeIndex = 0;
	bool bEnableRootMotion = false;
	bool bForceRootLock = false;
	bool bUseNormalizedRootMotionScale = false;
	uint8 RootMotionRootLock = 0; // ERootMotionRootLock
	float RateScale = 1.f;
	// 에디터에서 추가한 Notify. 재임포트할 때 기존 파일의 값을 유지한다(FFbxImportOptions::bKeepNotifies).
	TArray<FAnimNotifyRecord> Notifies;
};

struct FFbxMeshLODData
{
	FStaticMeshData MeshData;
	FSkinWeightData SkinWeights;
};

// Cook 결과다. SDK 포인터/GPU 리소스는 포함하지 않는다. 한 항목이 한 .fbxbin이다.
struct FFbxAssetData
{
	EFbxAssetType Type = EFbxAssetType::StaticMesh;
	FString Key;
	FString SourcePath; // 소유 FBX: Assets 상대 경로 또는 외부 원본의 절대 경로. 로딩 의존성이 아니다.
	FString SourceObject;
	FFbxImportOptions Options;
	// 실제로 읽은 파일. Reimport From File을 쓰면 소유 FBX와 달라질 수 있다.
	FString ActualSourcePath;
	uint64 SourceHash = 0; // 임포트 시점 ActualSourcePath의 내용 해시
	TArray<FFbxMeshLODData> LODs;
	TArray<FString> MaterialKeys;
	std::array<float, 3> ScreenThresholds{ .30f, .10f, .03f };
	FString SkeletonKey; // SkeletalMesh와 AnimSequence가 쓰는 Skeleton
	FReferenceSkeleton RefSkeleton;
	FFbxAnimationData Animation;
	FFbxMaterialData Material;
	FImageData Texture;

	bool Validate(FString& Error) const;
};

struct FFbxImportResult
{
	bool bSuccess = false;
	FString Error;
	TArray<FString> Warnings;
	TArray<FString> AssetKeys;
};
