#pragma once

#include <fbxsdk.h>
#include "Animation/AnimDataModel.h"
#include "Core/EngineString.h"
#include "MovieScene/MovieSceneFrameTime.h"
#include "Render/SkeletalMeshData.h"
#include "Asset/FbxImporter/FbxMeshBake.h"

class UAnimSequence;

struct FFbxAnimationImportOptions
{
	bool bResample = true; // false면 기본 30Hz 사용
	int32 ResampleRate = 0; // bResample이 true일 때 0은 자동 계산, 양수는 지정값
};

struct FFbxImportResult
{
	TArray<FString> Paths;
	TArray<FString> AnimationPaths; // 스택 1개면 Name.anim.bin, 여러 개면 Name_스택이름.anim.bin
	TArray<FString> Warnings;
	bool bSkipped = false;
};

// Import 중에만 사용하는 Node별 자료. SDK 포인터는 최종 엔진 출력에 넘기지 않는다.
struct FFbxMeshNodeData
{
	FbxNode* Node = nullptr;
	bool bHasSkin = false;
	int32 RigidBoneIndex = -1; // Skin 없는 Mesh가 선택된 Bone 계층에 속하면 자기 Node의 Bone 번호
	FStaticMeshData Geometry;
	TArray<int32> ControlPointIndices; // Geometry.Vertices와 같은 순서/길이
};

// Manager와 Scene의 수명을 관리하며 한 번에 FBX 파일 하나를 보관한다.
class FFbxImporter
{
public:
	FFbxImporter() = default;
	~FFbxImporter();
	FFbxImporter(const FFbxImporter&) = delete;
	FFbxImporter& operator=(const FFbxImporter&) = delete;

	// 이전 Scene을 닫고 새 파일을 읽는다. 실패하면 닫힌 상태와 오류를 반환한다.
	// 추출 폴더를 지정하면 Embedded Texture를 추출한다. 폴더는 호출자가 소유하며 Texture 복사가 끝난 뒤 정리한다.
	bool Open(const FString& Path, FString& OutError, const FString& ExtractionFolder = {});
	void Close();
	// 비소유 참조. 다음 Open/Close 또는 이 객체의 소멸 전까지만 유효하다.
	FbxScene* GetScene() const { return Scene; }
	// 열린 Scene의 축/단위를 맞추고 Node별 자료를 생성한다. 실패하면 Node 결과는 비운다.
	bool ReadMeshData(TArray<FString>& OutWarnings, FString& OutError);
	// 참조는 다음 ReadMeshData/Open/Close/소멸 전까지만 유효하다.
	const TArray<FFbxMeshNodeData>& GetMeshNodes() const { return MeshNodes; }
	// Import Static용: Skin 유무와 관계없이 모든 Node를 하나로 합친다.
	void BuildStaticMesh(FStaticMeshData& OutData) const;
	// ReadMeshData 성공 후 호출한다. Bone 계층/Bind와 rigid의 Bind 기준 Geometry를 만들며 Weight는 만들지 않는다.
	bool ReadSkeletonData(TArray<FString>& OutWarnings, FString& OutError);
	// 다음 Skeleton/Mesh 재추출, Open/Close/소멸 전까지만 유효하다.
	const TArray<FBoneInfo>& GetBones() const { return Bones; }
	// 새 Factory에서 사용할 Unreal 방식의 임포트 함수 틀.
	void ImportFromFile();
	void ImportStaticMeshAsSingle();
	void ImportSkeletalMesh();
	// ReadSkeletonData 성공 후 호출. 성공 시 OutSequences에 추가한 시퀀스의 수명은 호출부가 관리한다.
	// 스택마다 시간 범위 확인 → 프레임 경계 정렬 → 시퀀스 생성 → ImportAnimation 호출만 담당한다.
	// 키 추출과 PlayLength 확정은 ImportAnimation, 파일 저장은 ImportToAssets가 맡는다.
	bool ImportAnimations(TArray<UAnimSequence*>& OutSequences, TArray<FString>& OutWarnings,
		FString& OutError, const FFbxAnimationImportOptions& Options = {});
	// ReadSkeletonData 성공 후 호출한다. SDK 수명과 독립적인 Geometry/Weight/Bone 출력을 만든다.
	bool BuildSkeletalMesh(FSkeletalMeshData& OutData, TArray<FString>& OutWarnings, FString& OutError) const;
	// Assets/FBX에 저장한 뒤 미로드 Asset을 등록한다. 이미 로드된 객체는 유지한다.
	static bool ImportToAssets(const FString& Path, EFbxImportMode Mode, FFbxImportResult& OutResult, FString& OutError);

private:
	int32 GetMaxSampleRate() const;
	FbxTimeSpan GetAnimationTimeSpan(const FbxAnimStack& AnimStack) const;
	// 레이어·길이를 검사하고 Start/Stop을 SampleRate의 가장 가까운 프레임 경계로 맞춘다.
	bool ValidateAnimStack(const FbxAnimStack& AnimStack, FFrameRate SampleRate, FbxTimeSpan& InOutTimeSpan, FString& OutError) const;
	void ImportAnimation(UAnimSequence& Sequence, FbxAnimStack& AnimStack,
		FFrameRate SampleRate, const FbxTimeSpan& TimeSpan, TArray<FString>& OutWarnings);
	// 현재 활성 스택에서 SampleTimes 시각마다 본 Transform을 평가해 스켈레톤 순서의 본 트랙을 만든다.
	// 비정상(NaN·0 스케일) Transform이 나온 본은 트랙에서 빼고 경고를 남긴다. 그 본은 재생 시 바인드 포즈를 쓴다.
	void ImportBoneTracks(const TArray<FbxTime>& SampleTimes, TArray<FBoneAnimationTrack>& OutTracks, TArray<FString>& OutWarnings);

	void ClearSkeletonData();
	FbxManager* Manager = nullptr;
	FbxScene* Scene = nullptr;
	FString SourcePath;			// 원본 FBX 파일 경로 문자열
	TArray<FFbxMeshNodeData> MeshNodes;
	bool bIsBlender = false;	// Armature 예외 처리용
	TArray<FBoneInfo> Bones;
	TMap<FbxNode*, int32> BoneIndices; // Import 중 SDK Node를 출력 Bone 번호로 연결
	TMap<FbxNode*, FFbxMeshNodeData> RigidBindMeshes; // Skeleton과 함께 비우며, 현재 Global 기준 MeshNodes는 유지한다.
};
