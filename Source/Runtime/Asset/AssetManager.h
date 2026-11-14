#pragma once

#include "UObject/Object.h"
#include "UObject/Property.h"
#include "Render/Renderer.h"
#include "Asset/LOD/StaticMeshLODGenerator.h"
#include "Asset/AnimSequence.h"
#include "Asset/BlendSpace.h"

#include <filesystem>
#include <functional>
#include <set>

#include "AssetManager.generated.h"

enum class EAssetType
{

};

class UStaticMesh;
class USkeletalMesh;
class USkeleton;
class UBlendSpace1D;
class UBlendSpace2D;
class USoundWave;
class UParticleSystem;

namespace fs = std::filesystem;

UCLASS()
class UAssetManager : public UObject
{
	GENERATED_BODY()
private:
	UAssetManager() = default;

	UAssetManager(const UAssetManager& src) =  delete;
	UAssetManager& operator= (const UAssetManager& src) = delete;

	using FAssetLoadProgress = std::function<void(int32 Loaded, int32 Total, const FString& Path)>;
public:
	static UAssetManager& Get();
	static void Init(const FAssetLoadProgress& OnProgress = nullptr);
	static void Shutdown();

	void ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress);
	void LoadAsset(const FString& Key, const FString& Path);

	void CreateDefaultTextures();
	void CreateDefaultMeshes();
	void CreateDefaultMaterial();
	void CreateBillboardMaterial();
	void CreateParticleMaterial();
	void CreateParticleSpriteMaterial();
	void CreateEditorArrowMaterial();
	void CreateBoneWeightHeatmapMaterial();

	static UMaterial* LoadMaterialAsset(const FString& AssetPath);
	static bool SaveMaterialAsset(const FString& AssetPath, UMaterial& Material);
	void LoadDeferredAssets();

	template <typename T>
	static T* GetAssetByPath(const FString& InPath)
	{
		FString Normalized = InPath;
		for (char& C : Normalized)
		{
			if (C == '\\') C = '/';
		}

		URenderAsset** Found = Get().AssetMap.Find(Normalized);
		if (!Found)
		{
			if (Normalized.rfind("Assets/", 0) == 0)
			{
				Found = Get().AssetMap.Find(Normalized.substr(7));
			}
			else
			{
				Found = Get().AssetMap.Find("Assets/" + Normalized);
			}
		}

		if (Found == nullptr || *Found == nullptr || (*Found)->IsRetired() || !(*Found)->IsA<T>())
		{
			return nullptr;
		}
		return Cast<T>(*Found);
	}

	void RegisterAsset(const FString& Key, URenderAsset* Asset);
	void PrepareAssetRegistration(const TMap<FString, URenderAsset*>& Replacements, TMap<FString, URenderAsset*>& Prepared) const;
	void CommitAssetRegistration(TMap<FString, URenderAsset*>& Prepared) noexcept;

	//UStaticMesh* GetMesh(FString InName);
	//UStaticMesh* GetMesh(EPrimitiveType Type); // 오버로드(수정 중)

	UTexture2D* LoadTexture(const FString& InPath, bool bGenerateMips = true);
	UFont* LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath);
	USoundWave* LoadSound(const FString& Path);
	static UStaticMesh* LoadObjStaticMesh(const FString& Path);
	static USkeleton* LoadSkeleton(const FString& AssetPath);
	static UAnimSequence* LoadAnimSequence(const FString& AssetPath);
	static bool SaveAnimSequence(const UAnimSequence& Sequence);
	static UBlendSpace* LoadBlendSpace(const FString& AssetPath);
	static bool SaveBlendSpace1D(const FString& AssetPath, UBlendSpace1D& BlendSpace);
	static bool SaveBlendSpace2D(const FString& AssetPath, UBlendSpace2D& BlendSpace);
	static UParticleSystem* LoadParticleSystem(const FString& AssetPath);
	static bool SaveParticleSystem(const FString& AssetPath, UParticleSystem& ParticleSystem);
	static void GetAnimSequencesForSkeleton(const USkeleton* Skeleton, TArray<UAnimSequence*>& OutSequences);
	static void GetAllAnimSequences(TArray<UAnimSequence*>& OutSequences);
	static void GetSkeletalMeshesForSkeleton(const USkeleton* Skeleton, TArray<USkeletalMesh*>& OutMeshes);
	static USkeletalMesh* LoadFbxSkeletalMesh(const FString& Path);
	// 같은 Skeleton을 쓰는 로드된 시퀀스 목록(경로순). FBX를 다시 읽지 않는다.
	static const TArray<UAnimSequence*>* LoadFbxAnimSequences(const FString& Path, USkeleton& Skeleton);
	static const TArray<UAnimSequence*>* GetFbxAnimSequences(const FString& AssetKey);

	// 시작 스캔에서 바이너리를 찾지 못한 FBX 경로. 에디터가 임포트 창으로 넘긴다.
	const TArray<FString>& GetFbxImportCandidates() const { return FbxImportCandidates; }
	void MarkFbxImported(const FString& SourceIdentity);
	bool IsFbxImported(const FString& SourceIdentity) const;

	FLODGenerateResult GenerateStaticMeshLODs(UStaticMesh& Mesh,const FLODGenerateRequest& Request);

	// 원본 좌표축이 엔진과 다른 메시를 로드 후 돌린다 (정점·법선·AABB·삼각형 BVH·GPU 정점 버퍼를 다시 만든다).
	// LOD는 LOD0에서 만들어지므로 LOD를 만들기 전에만 부를 수 있다. Rotate는 회전(길이·손잡이 보존)이어야 한다.
	static bool ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate);

	// 로드된 OBJ 스태틱 메시(키가 .obj로 끝나는 것)마다 부른다. 기본 도형·기즈모 메시는 제외된다.
	static void ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func);

private:
	TMap<FString, FString> AssetPathMap;
	TMap<FString, URenderAsset*> AssetMap;
	TMap<FString, USoundWave*> SoundMap;
	TMap<FString, TArray<UAnimSequence*>> AnimationSequencesByFbx;
	TArray<FString> FbxImportCandidates;
	std::set<FString> ImportedFbxSources; // 소유 FBX 식별자, 대소문자 무시

	//TMap<FString, UStaticMesh*> MeshMap;	// KEY: FILE NAME OR 쉐입 첫글자 대문자
	TMap<FString, TUniquePtr<FShader>> ShaderMap;	// KEY: FILE NAME

	//TMap<FString, UTexture2D*> TextureMap;
};
