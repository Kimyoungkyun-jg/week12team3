#include "EnginePCH.h"
#include "FbxAssetLoader.h"
#include "FbxAssetBake.h"
#include "FbxAssetImporter.h"
#include "FbxPath.h"
#include "Asset/AssetManager.h"
#include "Animation/Skeleton.h"
#include "Asset/AnimSequence.h"
#include "Animation/AnimNotify.h"
#include "UObject/UObjectIterator.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderCommand.h"
#include "Render/Texture2D.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace
{
FSkeletalMeshData ConvertSkeletalLOD(const FFbxMeshLODData& Input, const FReferenceSkeleton& Ref)
{
	FSkeletalMeshData Out;
	Out.AABB = Input.MeshData.AABB;
	Out.Indices = Input.MeshData.Indices;
	for (const auto& Section : Input.MeshData.Sections)
		Out.Sections.Add({ Section.StartIndex, Section.IndexCount, Section.MaterialSlotIndex });
	for (const auto& Slot : Input.MeshData.MaterialSlots)
	{
		FSkeletalMaterialSlot Converted;
		static_cast<FImportedMaterialProperties&>(Converted) = Slot;
		Converted.Name = Slot.Name;
		Out.MaterialSlots.Add(Converted);
	}
	for (int32 Index = 0; Index < Input.MeshData.Vertices.Num(); ++Index)
	{
		const auto& Vertex = Input.MeshData.Vertices[Index];
		FSkeletalVertex Converted;
		Converted.Position = Vertex.Position; Converted.Normal = Vertex.Normal;
		Converted.Tangent = Vertex.Tangent; Converted.UV = Vertex.UV; Converted.Color = Vertex.Color;
		for (int I = 0; I < 4; ++I)
		{
			Converted.BoneIndices[I] = Input.SkinWeights.InfluenceBones[Index][I];
			Converted.BoneWeights[I] = Input.SkinWeights.InfluenceWeights[Index][I];
		}
		Out.SkinnedVertices.Add(Converted);
	}
	TArray<FMatrix> Globals;
	Ref.FillComponentSpaceTransforms(Ref.RefBonePose, Globals);
	for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
	{
		FBoneInfo Bone;
		Bone.Name = Ref.RefBoneInfo[Index].Name; Bone.ParentIndex = Ref.RefBoneInfo[Index].ParentIndex;
		Bone.LocalBindPose = Ref.RefBonePose[Index].ToMatrix(); Bone.GlobalBindPose = Globals[Index];
		Bone.InverseBindPose = Globals[Index].Inverse();
		Out.Bones.Add(Bone);
	}
	return Out;
}

// 파일의 트랙은 본 이름으로 연결되고 일부 본이 빠질 수 있다(Target Skeleton 임포트).
// 런타임 샘플러는 Skeleton 본 순서와 같은 개수의 트랙을 요구하므로 여기서 본 순서로 다시 놓는다. 트랙이 없는 본은 기준 포즈 키 1개를 쓴다.
bool BuildSequence(const FFbxAssetData& Asset, USkeleton& Skeleton, UAnimSequence& Sequence, FString& Error)
{
	const FFbxAnimationData& Source = Asset.Animation;
	const FReferenceSkeleton& Ref = Skeleton.RefSkeleton;
	const int32 BoneCount = Ref.GetNum();
	TArray<FBoneAnimationTrack> Tracks;
	Tracks.SetNum(BoneCount);
	TArray<uint8> Filled;
	Filled.SetNum(BoneCount);
	int32 Matched = 0;
	for (const FFbxBoneTrack& Track : Source.Tracks)
	{
		int32 Bone = Ref.FindBoneIndex(Track.BoneName);
		// 이 수정 전에 저장된 파일은 이름이 FName을 거쳐 "spine_01"이 "spine_1"로 바뀌어 있다. FName 비교로 한 번 더 찾는다.
		if (Bone < 0)
		{
			const FName Name(Track.BoneName);
			for (int32 Index = 0; Index < BoneCount && Bone < 0; ++Index)
				if (FName(Ref.RefBoneInfo[Index].Name) == Name) Bone = Index;
		}
		if (Bone < 0 || Filled[Bone]) continue;
		Tracks[Bone].InternalTrack = Track.InternalTrack;
		Filled[Bone] = 1;
		++Matched;
	}
	if (!Matched) { Error = "No animation track matches the skeleton: " + Asset.Key; return false; }
	for (int32 Bone = 0; Bone < BoneCount; ++Bone)
	{
		Tracks[Bone].Name = FName(Ref.RefBoneInfo[Bone].Name);
		if (Filled[Bone]) continue;
		const FBoneTransform& Pose = Ref.RefBonePose[Bone];
		Tracks[Bone].InternalTrack.PosKeys = { Pose.Translation };
		Tracks[Bone].InternalTrack.RotKeys = { Pose.Rotation };
		Tracks[Bone].InternalTrack.ScaleKeys = { Pose.Scale };
	}
	if (Matched < Source.Tracks.Num())
		HTR_LOG(Warning, "[Anim] {}: {} tracks have no bone in {}", Asset.Key, Source.Tracks.Num() - Matched, Asset.SkeletonKey);
#ifdef ENGINE_DEBUG
	// 임포트·저장·샘플링이 맞물리는지 보는 점검. 0초 키를 Skeleton의 기준 포즈와 비교한다.
	// 키가 1개인 본(움직이지 않는 본)은 기준 포즈와 같아야 한다. 차이가 크면 공간 변환이 어긋난 것이다.
	{
		float StaticPosition = 0.f, StaticDegrees = 0.f;
		int32 StaticCount = 0;
		FString WorstBone;
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			const FRawAnimSequenceTrack& Keys = Tracks[Bone].InternalTrack;
			if (!Filled[Bone] || Keys.PosKeys.Num() != 1 || Keys.RotKeys.Num() != 1) continue;
			const FBoneTransform& Bind = Ref.RefBonePose[Bone];
			const FQuat& A = Keys.RotKeys[0];
			const FQuat& B = Bind.Rotation;
			const float Dot = std::min(1.0f, std::abs(A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W));
			const float Degrees = 2.0f * std::acos(Dot) * 57.2957795f;
			StaticPosition = std::max(StaticPosition, (Keys.PosKeys[0] - Bind.Translation).Length());
			if (Degrees > StaticDegrees) { StaticDegrees = Degrees; WorstBone = Ref.RefBoneInfo[Bone].Name; }
			++StaticCount;
		}
		HTR_LOG(Info, "[Anim] {}: {}/{} bones matched, {} keys | static bones {}: max position {:.4f}, rotation {:.3f} deg ({})",
			Asset.Key, Matched, BoneCount, Source.NumberOfKeys, StaticCount, StaticPosition, StaticDegrees, WorstBone);
	}
#endif
	UAnimDataModel* Model = Sequence.DataModel ? Sequence.DataModel : NewObject<UAnimDataModel>(&Sequence);
	Model->BoneAnimationTracks = std::move(Tracks);
	Model->FrameRate = Source.FrameRate;
	Model->NumberOfKeys = Source.NumberOfKeys;
	Model->NumberOfFrames = Source.NumberOfKeys - 1;
	// 한 키짜리 포즈 애니메이션도 0으로 나누지 않도록 최소 한 프레임 길이를 준다.
	Model->PlayLength = static_cast<float>(std::max(1, Model->NumberOfFrames)) / Source.FrameRate.AsDecimal();
	Sequence.DataModel = Model;
	Sequence.Skeleton = &Skeleton;

	// 파일에 저장된 Notify로 객체를 만든다. DataModel을 설정한 뒤여야 시간이 시퀀스 길이로 맞춰진다.
	Sequence.ImportNotifies(Source.Notifies);

	Sequence.Name = Source.Name;
	Sequence.SourceFilePath = Asset.SourcePath;
	Sequence.SourceTakeIndex = Source.SourceTakeIndex;
	Sequence.bEnableRootMotion = Source.bEnableRootMotion;
	Sequence.bForceRootLock = Source.bForceRootLock;
	Sequence.bUseNormalizedRootMotionScale = Source.bUseNormalizedRootMotionScale;
	Sequence.RootMotionRootLock = static_cast<ERootMotionRootLock>(Source.RootMotionRootLock);
	Sequence.RateScale = Source.RateScale;
	return true;
}

// 사용자가 바꾼 루트 모션 설정을 재임포트 후에도 유지한다. 기존 .fbxbin이 없으면 예전 .animseq.bin에서 옮긴다.
void KeepAnimationSettings(const fs::path& Target, FFbxAssetData& Data)
{
	FFbxAssetData Existing; FString Ignored;
	auto& A = Data.Animation;
	if (fs::exists(Target) && FFbxAssetBake::ReadBaked(FbxPath::ToString(Target), Existing, Ignored) &&
		Existing.Type == EFbxAssetType::AnimSequence)
	{
		const auto& E = Existing.Animation;
		A.bEnableRootMotion = E.bEnableRootMotion; A.bForceRootLock = E.bForceRootLock;
		A.bUseNormalizedRootMotionScale = E.bUseNormalizedRootMotionScale;
		A.RootMotionRootLock = E.RootMotionRootLock; A.RateScale = E.RateScale;
		// Notify는 옵션이 켜져 있을 때만 유지한다 (기본값은 유지). 끄면 새 파일에는 Notify가 없다.
		if (Data.Options.bKeepNotifies) A.Notifies = E.Notifies;
		return;
	}
	// 예전 형식은 FBX 옆의 <이름>.animseq.bin(스택 하나) 또는 <이름>_<번호>.animseq.bin이었다.
	fs::path Source;
	if (!FbxPath::ResolveKey(Data.SourcePath, Source, Ignored)) return;
	const std::wstring Stem = Source.stem().native();
	for (const std::wstring& Name : { Stem + L".animseq.bin", Stem + L"_" + std::to_wstring(A.SourceTakeIndex) + L".animseq.bin" })
	{
		if (FFbxAssetBake::ReadLegacyAnimationSettings(FbxPath::ToString(Source.parent_path() / Name), A))
		{
			HTR_LOG(Info, "[Anim] {}: root motion settings migrated from {}", Data.Key, FbxPath::ToString(Name));
			return;
		}
	}
}

// 아직 등록하지 않은 의존 애셋도 같은 배치에서 찾는다. 실패 시 기존 검색표는 그대로 유지된다.
class FLoadBatch
{
public:
	std::map<FString, FFbxAssetData> Data;
	std::map<FString, URenderAsset*> Objects;
	std::set<FString> Loading;
	std::vector<URenderAsset*> Created;
	FString Error;
	bool bPublished = false;
	bool bPrepared = false;
	TMap<FString, URenderAsset*> PreparedRegistry;
	bool bAllowDiskDependencies = true;
	std::function<URenderAsset*(const FImageData&, FString&)> TextureFactory;

	~FLoadBatch()
	{
		if (!bPublished)
			for (auto It = Created.rbegin(); It != Created.rend(); ++It) delete *It;
	}

	template<class T> T* Allocate()
	{
		T* Object = NewObject<T>();
		if (Object) Created.push_back(Object);
		return Object;
	}

	URenderAsset* Resolve(const FString& Key)
	{
		if (auto It = Objects.find(Key); It != Objects.end()) return It->second;
		if (!Data.contains(Key))
		{
			if (auto* Existing = UAssetManager::GetAssetByPath<URenderAsset>(Key)) return Existing;
			if (!bAllowDiskDependencies) { Error = "Missing imported dependency: " + Key; return nullptr; }
			fs::path Path;
			if (!FbxPath::ResolveKey(Key, Path, Error)) return nullptr;
			FFbxAssetData Asset;
			if (Path.extension() != ".fbxbin" || !FFbxAssetBake::ReadBaked(FbxPath::ToString(Path), Asset, Error)) return nullptr;
			Asset.Key = Key; Data.emplace(Key, std::move(Asset));
		}
		if (!Loading.insert(Key).second) { Error = "Cyclic FBX asset reference: " + Key; return nullptr; }
		const auto& Asset = Data.at(Key);
		URenderAsset* Object = nullptr;
		if (Asset.Type == EFbxAssetType::Texture2D)
		{
			Object = TextureFactory(Asset.Texture, Error);
			if (Object) Created.push_back(Object);
		}
		else if (Asset.Type == EFbxAssetType::Skeleton)
		{
			auto* Skeleton = Allocate<USkeleton>();
			Skeleton->RefSkeleton = Asset.RefSkeleton; Object = Skeleton;
		}
		else if (Asset.Type == EFbxAssetType::AnimSequence)
		{
			// 애니메이션은 Skeleton에 맞춰 만들어진다. 같은 배치, 등록된 애셋, 디스크 순서로 Skeleton을 찾는다.
			auto* Skeleton = Cast<USkeleton>(Resolve(Asset.SkeletonKey));
			if (!Skeleton) { if (Error.empty()) Error = "Invalid FBX skeleton reference: " + Asset.SkeletonKey; return nullptr; }
			auto* Sequence = Allocate<UAnimSequence>();
			if (!Sequence || !BuildSequence(Asset, *Skeleton, *Sequence, Error)) return nullptr;
			Object = Sequence;
		}
		else if (Asset.Type == EFbxAssetType::Material)
		{
			auto* Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
			if (!Base) { Error = "DefaultMaterial must be initialized before loading FBX materials"; return nullptr; }
			auto* Material = UMaterial::CreateInstance(Base);
			if (!Material) { Error = "Cannot create FBX material"; return nullptr; }
			Created.push_back(Material); Object = Material;
			if ((Base->ParamBuffer && !Material->ParamBuffer) || Material->Textures.Num() < 2)
			{ Error = "Cannot initialize FBX material resources"; return nullptr; }
			Material->BaseColor = Asset.Material.DiffuseOrFactor;
			Material->Shininess = Asset.Material.Shininess;
			Material->SpecularColor = Asset.Material.SpecularColor;
			Material->SpecularFactor = Asset.Material.SpecularFactor;
			Material->EmissiveColor = Asset.Material.EmissiveColor;
			Material->EmissiveFactor = Asset.Material.EmissiveFactor;
			Material->ShadingModel = Asset.Material.ShadingModel;
			Material->bOverrideShadingModel = true;
			Material->SamplerState = Asset.Material.SamplerState;
			Material->UVTiling = FVector2(1, 1);
			Material->UVScrollSpeed = FVector2(0, 0);
			Material->WorldUVTileSize = FVector2(0, 0);
			Material->SetBlendState(Asset.Material.BlendState);
			const FString Keys[]{ Asset.Material.DiffuseTexture, Asset.Material.NormalTexture };
			for (int I = 0; I < 2; ++I) if (!Keys[I].empty())
			{
				auto* Texture = Cast<UTexture2D>(Resolve(Keys[I]));
				if (!Texture) { if (Error.empty()) Error = "Invalid FBX texture reference: " + Keys[I]; return nullptr; }
				Material->Textures[I] = Texture;
			}
		}
		else
		{
			TArray<UMaterial*> Materials;
			for (const auto& MaterialKey : Asset.MaterialKeys)
			{
				auto* Material = Cast<UMaterial>(Resolve(MaterialKey));
				if (!Material) { if (Error.empty()) Error = "Invalid FBX material reference: " + MaterialKey; return nullptr; }
				Materials.Add(Material);
			}
			if (Asset.Type == EFbxAssetType::StaticMesh)
			{
				auto* Mesh = Allocate<UStaticMesh>(); Object = Mesh;
				Mesh->Materials = std::move(Materials); Mesh->ScreenThresholds = Asset.ScreenThresholds;
				for (int32 L = 0; L < Asset.LODs.Num(); ++L)
				{
					FStaticMeshLODResource Resource;
					Resource.Data = Asset.LODs[L].MeshData;
					// Cook에서 완성한 UV와 탄젠트를 사용한다.
					Resource.Data.OptimizeTriangleOrderForVertexCache();
					if (!Resource.Data.Indices.IsEmpty())
					{
						Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(Resource.Data.Vertices.GetData(),
							static_cast<uint32>(Resource.Data.Vertices.Num() * sizeof(FVertex)), sizeof(FVertex));
						Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Resource.Data.Indices.GetData(), Resource.Data.Indices.Num());
						if (!Resource.VertexBuffer || !Resource.IndexBuffer) { Error = "Cannot create FBX mesh GPU buffers"; return nullptr; }
					}
					Resource.Data.BuildTriangleBVH();
					if (L == 0)
					{
						Mesh->MeshData = std::move(Resource.Data); Mesh->VertexBuffer = std::move(Resource.VertexBuffer);
						Mesh->IndexBuffer = std::move(Resource.IndexBuffer);
					}
					else Mesh->AdditionalLODs.Add(std::move(Resource));
				}
				Mesh->RebuildRenderData();
			}
			else if (Asset.Type == EFbxAssetType::SkeletalMesh)
			{
				auto* Skeleton = Cast<USkeleton>(Resolve(Asset.SkeletonKey));
				if (!Skeleton) { if (Error.empty()) Error = "Invalid FBX skeleton reference"; return nullptr; }
				auto* Mesh = Allocate<USkeletalMesh>(); Object = Mesh;
				Mesh->Skeleton = Skeleton; Mesh->RefSkeleton = Asset.RefSkeleton;
				Mesh->Materials = std::move(Materials); Mesh->ScreenThresholds = Asset.ScreenThresholds;
				for (int32 L = 0; L < Asset.LODs.Num(); ++L)
				{
					FSkeletalMeshLODResource Resource;
					Resource.Data = ConvertSkeletalLOD(Asset.LODs[L], Asset.RefSkeleton);
					Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(Resource.Data.SkinnedVertices.GetData(),
						static_cast<uint32>(Resource.Data.SkinnedVertices.Num() * sizeof(FSkeletalVertex)), sizeof(FSkeletalVertex));
					Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Resource.Data.Indices.GetData(), Resource.Data.Indices.Num());
					if (!Resource.VertexBuffer || !Resource.IndexBuffer) { Error = "Cannot create imported skeletal GPU buffers"; return nullptr; }
					if (L == 0) { Mesh->MeshData = std::move(Resource.Data); Mesh->VertexBuffer = std::move(Resource.VertexBuffer); Mesh->IndexBuffer = std::move(Resource.IndexBuffer); }
					else Mesh->AdditionalLODs.Add(std::move(Resource));
				}
				Mesh->RebuildRenderData();
			}
		}
		Loading.erase(Key);
		if (!Object) { if (Error.empty()) Error = "Cannot create FBX asset: " + Key; return nullptr; }
		Object->SetPath(Key); Objects.emplace(Key, Object);
		return Object;
	}

	void PreparePublish()
	{
		TMap<FString, URenderAsset*> Replacements;
		for (const auto& [Key, Object] : Objects)
			Replacements.Add(Key, Object);
		UAssetManager::Get().PrepareAssetRegistration(Replacements, PreparedRegistry);
		bPrepared = true;
	}
	void Publish()
	{
		if (!bPrepared) PreparePublish();
		UAssetManager::Get().CommitAssetRegistration(PreparedRegistry);
		bPublished = true;
	}
};

// 임시 파일은 Assets 안의 이번 임포트 전용 폴더에만 만든다.
struct FImportFolder
{
	fs::path Path;
	~FImportFolder()
	{
		FString Key, Error;
		if (!Path.empty() && Path.filename().wstring().starts_with(L".fbx-import-") &&
			FbxPath::GetAssetKey(FbxPath::ToString(Path), Key, Error, false))
		{
			std::error_code Ignored; fs::remove_all(Path, Ignored);
		}
	}
};
}

URenderAsset* FFbxAssetLoader::CreateTexture(const FImageData& Image, FString& Error)
{
	FTextureDesc Desc{};
	const bool GenerateMips = Image.BytesPerPixel != 0;
	Desc.Width = Image.Width; Desc.Height = Image.Height; Desc.MipLevels = GenerateMips ? 0 : 1; 
	Desc.Usage = GenerateMips ? ETextureUsage::Default : ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource | (GenerateMips ? ETextureBindFlags::RenderTarget : ETextureBindFlags::None);
	Desc.bGenerateMips = GenerateMips;
	auto Resource = RenderCommand::CreateTexture2D(Desc, Image);
	if (!Resource || !Resource->GetSRV()) { Error = "Cannot create FBX texture GPU resource"; return nullptr; }
	auto* Object = NewObject<UTexture2D>(); Object->SetResource(std::move(Resource)); return Object;
}

void FFbxAssetLoader::RebindToReimportedSkeletons()
{
	// 메시 FBX를 다시 가져오면 Skeleton 객체가 새로 만들어지고 옛 객체는 Retired가 된다.
	// 다른 FBX에서 만든 AnimSequence(Target Skeleton 임포트)와 BlendSpace는 옛 Skeleton을 가리킨 채 남으므로,
	// 객체는 그대로 두고 Skeleton을 새 객체로 바꾼다. 시퀀스는 본 순서가 바뀌었을 수 있어 파일에서 트랙을 다시 놓는다.
	TArray<UAnimationAsset*> Stale;
	for (TObjectIterator<UAnimationAsset> It; It; ++It)
	{
		UAnimationAsset* Asset = *It;
		if (Asset && !Asset->IsRetired() && Asset->Skeleton && Asset->Skeleton->IsRetired())
			Stale.Add(Asset);
	}
	for (UAnimationAsset* Asset : Stale)
	{
		USkeleton* Current = UAssetManager::GetAssetByPath<USkeleton>(Asset->Skeleton->GetPath());
		if (!Current || Current->IsRetired()) continue;
		if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
		{
			FString Path, Error;
			FFbxAssetData Data;
			if (!FbxPath::ResolveKey(Sequence->GetPath(), Path, Error) || !FFbxAssetBake::ReadBaked(Path, Data, Error) ||
				Data.Type != EFbxAssetType::AnimSequence || !BuildSequence(Data, *Current, *Sequence, Error))
			{
				HTR_LOG(Warning, "[Anim] Cannot rebind {} to the reimported skeleton: {}", Sequence->GetPath(), Error);
				continue;
			}
		}
		else
			Asset->Skeleton = Current;
	}
}

URenderAsset* FFbxAssetLoader::LoadAsset(const FString& Path, FString& Error)
{
	try
	{
		FString Key;

		if (!FbxPath::GetAssetKey(Path, Key, Error))
		{
			return nullptr;
		}

		if (auto* Existing = UAssetManager::GetAssetByPath<URenderAsset>(Key))
		{
			return Existing;
		}

		FLoadBatch Batch;
		Batch.TextureFactory = CreateTexture;
		auto* Object = Batch.Resolve(Key);

		if (!Object)
		{
			Error = Batch.Error;
			return nullptr;
		}

		Batch.Publish();
		return Object;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return nullptr;
	}
}

FFbxImportResult FFbxAssetLoader::Import(const FString& SourcePath, const FFbxImportOptions& Options, const FString& OutputDirectory, const FString& OwnerSource)
{
	FFbxImportResult Result;
	try
	{
		FString SourceKey;
		fs::path Source, Destination;
		if (!FbxPath::GetSourcePath(SourcePath, Source, SourceKey, Result.Error) ||
			!FbxPath::GetOutputDirectory(OutputDirectory.empty() ? FbxPath::DefaultOutputDirectory(SourcePath) : OutputDirectory, Destination, Result.Error)) return Result;
		fs::create_directories(Destination);
		FImportFolder Folder;
		for (uint32 I = 0; I < 100; ++I)
		{
			Folder.Path = Destination / (".fbx-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(I));
			if (fs::create_directory(Folder.Path)) break;
			Folder.Path.clear();
		}
		if (Folder.Path.empty()) { Result.Error = "Cannot create FBX staging directory"; return Result; }
		TArray<FFbxAssetData> Cooked;
		if (!FFbxAssetImporter::Cook(FbxPath::ToString(Source), Options, FbxPath::ToString(Folder.Path), Cooked, Result.Warnings, Result.Error,
			FbxPath::ToString(Destination), OwnerSource)) return Result;
		FLoadBatch Batch; Batch.TextureFactory = CreateTexture; Batch.bAllowDiskDependencies = false;
		struct FReplacement { fs::path Target, Staged, Backup; bool bBackedUp = false, bInstalled = false; };
		std::vector<FReplacement> Files;
		TArray<FString> OutputKeys;
		for (auto& Data : Cooked)
		{
			fs::path Target;
			if (!FbxPath::ResolveKey(Data.Key, Target, Result.Error)) return Result;
			FReplacement File{ Target, Folder.Path / (std::to_string(Files.size()) + ".new"), Folder.Path / (std::to_string(Files.size()) + ".old") };
			if (Data.Type == EFbxAssetType::AnimSequence) KeepAnimationSettings(Target, Data);
			if (!FFbxAssetBake::WriteBaked(FbxPath::ToString(File.Staged), Data, Result.Error)) return Result;
			FFbxAssetData Verified;
			if (!FFbxAssetBake::ReadBaked(FbxPath::ToString(File.Staged), Verified, Result.Error)) return Result;
			Verified.Key = Data.Key;
			if (!Batch.Data.emplace(Data.Key, std::move(Verified)).second) { Result.Error = "Duplicate FBX output key"; return Result; }
			Files.push_back(std::move(File)); OutputKeys.Add(Data.Key);
		}
		for (const auto& Key : OutputKeys)
			if (!Batch.Resolve(Key)) { Result.Error = Batch.Error; return Result; }
		Batch.PreparePublish();
		// 모든 데이터/리소스가 준비된 뒤 파일을 교체한다. 중간 실패 시 백업을 되돌린다.
		try
		{
			for (auto& File : Files)
			{
				if (fs::exists(File.Target)) { fs::rename(File.Target, File.Backup); File.bBackedUp = true; }
				fs::rename(File.Staged, File.Target); File.bInstalled = true;
			}
		}
		catch (const std::exception& E)
		{
			Result.Error = FString("Cannot replace FBX outputs: ") + E.what();
			bool Restored = true;
			for (auto It = Files.rbegin(); It != Files.rend(); ++It)
			{
				std::error_code Ec;
				if (It->bInstalled) fs::remove(It->Target, Ec);
				if (Ec) Restored = false;
				Ec.clear();
				if (It->bBackedUp) fs::rename(It->Backup, It->Target, Ec);
				if (Ec) Restored = false;
			}
			// 복원까지 실패한 경우 복구용 백업을 지우지 않는다.
			if (!Restored) { Result.Error += "; backups retained at " + FbxPath::ToString(Folder.Path); Folder.Path.clear(); }
			return Result;
		}
		Batch.Publish(); Result.AssetKeys = std::move(OutputKeys); Result.bSuccess = true;
		RebindToReimportedSkeletons();
		UAssetManager::Get().MarkFbxImported(OwnerSource.empty() ? SourceKey : OwnerSource);
	}
	catch (const std::exception& E) { Result.Error = E.what(); }
	return Result;
}
