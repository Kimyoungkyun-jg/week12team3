#pragma once

#include "UObject/Object.h"
#include "UObject/Property.h"
#include "Render/Renderer.h"
#include "Asset/LOD/StaticMeshLODGenerator.h"

#include <filesystem>
#include <functional>

#include "AssetManager.generated.h"

enum class EAssetType
{
	None,
	Texture,
	Font,
	StaticMesh,
	SkeletalMesh
};

class UStaticMesh;
class USoundWave;
class UAnimSequence;
class UBlendSpace1D;
struct FSkeletalMaterialSlot;

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

	// KeyPrefix가 있으면 키와 경로를 KeyPrefix + 상대 경로로 등록한다 (엔진 콘텐츠의 "Engine/").
	void ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress, const FString& KeyPrefix = {});
	void LoadAsset(const FString& Key, const FString& Path);

	void CreateDefaultTextures();
	void CreateDefaultMeshes();
	void CreateDefaultMaterial();
	void CreateBillboardMaterial();
	void CreateParticleMaterial();
	void CreateEditorArrowMaterial();

	// 경로는 "Assets/Models/a.obj"와 "Models/a.obj"를 같은 에셋으로 본다.
	// 아직 로드되지 않았으면 여기서 로드하므로 GPU 자원을 만들 수 있는 메인 스레드에서만 부른다.
	template <typename T>
	static T* GetAssetByPath(const FString& Path)
	{
		URenderAsset* Asset = Get().FindOrLoadAsset(Path);

		if (Asset == nullptr || !Asset->IsA<T>())
		{
			return nullptr;
		}
		return Cast<T>(Asset);
	}

	// 로드를 일으키지 않고 이미 로드된 에셋만 찾는다
	static URenderAsset* FindLoadedAsset(const FString& Path);

	// 스캔으로 등록됐지만 아직 로드되지 않은 에셋의 타입. 그 외에는 None
	static EAssetType GetUnloadedAssetType(const FString& Path);
	static TArray<FString> GetUnloadedAssetPaths(EAssetType Type);

	void RegisterAsset(const FString& Key, URenderAsset* Asset);

	//UStaticMesh* GetMesh(FString InName);
	//UStaticMesh* GetMesh(EPrimitiveType Type); // 오버로드(수정 중)

	UTexture2D* LoadTexture(const FString& InPath, bool bGenerateMips = true);
	UFont* LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath);
	USoundWave* LoadSound(const FString& Path);
	static UStaticMesh* LoadObjStaticMesh(const FString& Path);
	static URenderAsset* LoadFbxMesh(const FString& Path, FString& OutError);
	static UAnimSequence* LoadAnimSequence(const FString& Path);
	static UBlendSpace1D* LoadBlendSpace(const FString& Path);

	FLODGenerateResult GenerateStaticMeshLODs(UStaticMesh& Mesh,const FLODGenerateRequest& Request);

	// 원본 좌표축이 엔진과 다른 메시를 로드 후 돌린다 (정점·법선·AABB·삼각형 BVH·GPU 정점 버퍼를 다시 만든다).
	// LOD는 LOD0에서 만들어지므로 LOD를 만들기 전에만 부를 수 있다. Rotate는 회전(길이·손잡이 보존)이어야 한다.
	static bool ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate);

	// OBJ 스태틱 메시(키가 .obj로 끝나는 것)마다 부른다. 아직 로드되지 않은 메시는 먼저 로드한다. 기본 도형·기즈모 메시는 제외된다.
	static void ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func);
	static bool BuildMaterialsFromSlots(const TArray<FStaticMaterialSlot>& Slots, TArray<UMaterial*>& OutMaterials);
	static bool BuildMaterialsFromSlots(const TArray<FSkeletalMaterialSlot>& Slots, TArray<UMaterial*>& OutMaterials);

private:
	URenderAsset* FindOrLoadAsset(const FString& Path);

	// 키는 Assets 기준 상대 경로. 로드 가능한 파일의 키 -> 실제 파일 경로
	TMap<FString, FString> AssetPathMap;
	TMap<FString, URenderAsset*> AssetMap;
	TMap<FString, USoundWave*> SoundMap;
	TMap<FString, UAnimSequence*> AnimSequenceMap;
	TMap<FString, UBlendSpace1D*> BlendSpaceMap;

	//TMap<FString, UStaticMesh*> MeshMap;	// KEY: FILE NAME OR 쉐입 첫글자 대문자
	TMap<FString, TUniquePtr<FShader>> ShaderMap;	// KEY: FILE NAME

	//TMap<FString, UTexture2D*> TextureMap;
};
