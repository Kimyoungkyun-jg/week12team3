#include "EnginePCH.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/RenderPacket.h"
#include "Collision/Ray.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/World.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Render/SkinningStats.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AnimGraphAsset.h"
#include "Asset/AssetManager.h"
#include "Components/CharacterMovementComponent.h"
#include "GameFramework/Character.h"

namespace
{
	// 섞인 Bone Matrix의 0~2열. Affine이므로 3열은 (0,0,0,1)이므로 계산하지 않음
	using FBlendMatrix = float[4][3];

	// 점 변환 (행 벡터): pxM = x*Row0 + y*Row1 + z*Row2 + Row3
	inline void TransformPoint(const FBlendMatrix& B, const FVector& P, FVector& Out)
	{
		Out.X = P.X * B[0][0] + P.Y * B[1][0] + P.Z * B[2][0] + B[3][0];
		Out.Y = P.X * B[0][1] + P.Y * B[1][1] + P.Z * B[2][1] + B[3][1];
		Out.Z = P.X * B[0][2] + P.Y * B[1][2] + P.Z * B[2][2] + B[3][2];
	}

	// 방향 변환: 이동(Row3) 없이 변환한 뒤 정규화. 너무 짧으면 0.
	inline void TransformDirection(const FBlendMatrix& B, float X, float Y, float Z, float* Out)
	{
		const float RX = X * B[0][0] + Y * B[1][0] + Z * B[2][0];
		const float RY = X * B[0][1] + Y * B[1][1] + Z * B[2][1];
		const float RZ = X * B[0][2] + Y * B[1][2] + Z * B[2][2];
		const float LengthSq = RX * RX + RY * RY + RZ * RZ;
		const float InvLength = LengthSq > SMALL_NUMBER ? 1.0f / std::sqrt(LengthSq) : 0.0f;
		Out[0] = RX * InvLength;
		Out[1] = RY * InvLength;
		Out[2] = RZ * InvLength;
	}

	// Bone Weight Heatmap: 정점이 Bone 하나에게서 받는 가중치. 4개 슬롯 중 그 본을 가리키는 슬롯의 값을 더한다.
	// 선택된 본이 없으면(-1) 어느 슬롯과도 같지 않으므로 0이다.
	inline float GetBoneWeight(const FSkeletalVertex& Vertex, int32 BoneIndex)
	{
		float Weight = 0.0f;
		for (int32 i = 0; i < 4; ++i)
			if (Vertex.BoneIndices[i] == BoneIndex) Weight += Vertex.BoneWeights[i];
		return Weight;
	}

	// 가중치 0~1을 색으로 바꾼다. 0 파랑 → 0.25 하늘 → 0.5 초록 → 0.75 노랑 → 1 빨강
	inline FVector4 WeightToHeatColor(float Weight)
	{
		const float W = std::clamp(Weight, 0.0f, 1.0f);
		const float R = std::clamp(4.0f * W - 2.0f, 0.0f, 1.0f);
		const float G = std::clamp(W < 0.5f ? 4.0f * W : 4.0f - 4.0f * W, 0.0f, 1.0f);
		const float B = std::clamp(2.0f - 4.0f * W, 0.0f, 1.0f);
		return FVector4(R, G, B, 1.0f);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	delete AnimScriptInstance;
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	InitBoneTransforms();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	// Any main view that gathered this component since the previous tick can keep it active.
	const bool bTickPose = !bOnlyTickPoseWhenRendered || bVisibleSinceLastTick;
	bVisibleSinceLastTick = false;
	if (!bTickPose) return;

	EnsureBoneState();
	if (AnimScriptInstance && SkeletalMesh)
	{
		AnimScriptInstance->UpdateAnimation(DeltaTime);
		TArray<FMatrix> Pose;
		if (AnimScriptInstance->EvaluatePose(Pose) && Pose.Num() == LocalBoneTransforms.Num())
		{
			LocalBoneTransforms = std::move(Pose);
			bGlobalBoneTransformsOverridden = false;
			bSkinningMatricesDirty = true;
			FMatrix RootMotion;
			if (AnimScriptInstance->ConsumeRootMotion(RootMotion) && GetWorld() && GetWorld()->IsGameWorld())
				if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
					if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
						Movement->AddRootMotion(RootMotion);
		}
	}

	if (NeedsSkinningUpdate())
	{
		UpdateSkinning();
	}

	// Notify는 본 행렬 갱신까지 끝난 뒤에 호출한다. 호출된 동작이 이번 프레임의 본 위치를 읽을 수 있다.
	if (AnimScriptInstance && SkeletalMesh)
	{
		AnimScriptInstance->TriggerAnimNotifies();
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FString AnimClassName = AnimClass ? AnimClass->Name : FString{};
	if (auto Slot = Record.TryEnterField("AnimClassName", true))
	{
		Slot.GetValue() << AnimClassName;
		if (Record.GetUnderlyingArchive().IsLoading())
			SetAnimInstanceClass(AnimClassName.empty() ? nullptr : FindClass(AnimClassName));
	}
	FString GraphPath = AnimGraphPath;
	if (auto Slot = Record.TryEnterField("AnimGraphPath", true))
	{
		Slot.GetValue() << GraphPath;
		if (Record.GetUnderlyingArchive().IsLoading() && !GraphPath.empty()) SetAnimGraphPath(GraphPath);
	}
	FString SequencePath = AnimSequence ? AnimSequence->GetPath() : FString{};
	if (auto Slot = Record.TryEnterField("AnimSequencePath", true))
	{
		Slot.GetValue() << SequencePath;
		if (Record.GetUnderlyingArchive().IsLoading())
			SetAnimationSequence(SequencePath.empty() ? nullptr : UAssetManager::LoadAnimSequence(SequencePath));
	}
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	Super::SetSkeletalMesh(InSkeletalMesh);
	InitBoneTransforms();
	if (AnimSequence && (!SkeletalMesh || AnimSequence->Skeleton != SkeletalMesh->Skeleton))
		SetAnimationSequence(nullptr);
	if (AnimScriptInstance)
	{
		AnimScriptInstance->InitializeAnimation();
		if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimScriptInstance))
		{
			if (SingleNode->SetAnimationAsset(AnimSequence) && AnimSequence) SingleNode->Play();
		}
	}
}

bool USkeletalMeshComponent::SetAnimInstanceClass(UClass* InAnimClass)
{
	if (InAnimClass && (!InAnimClass->IsChildOf(UAnimInstance::StaticClass()) || !InAnimClass->Constructor))
		return false;
	if (InAnimClass != UAnimGraphInstance::StaticClass()) AnimGraphPath.clear();
	if (AnimClass == InAnimClass && (!InAnimClass || AnimScriptInstance)) return true;
	UAnimInstance* NewInstance = InAnimClass ? NewObject<UAnimInstance>(this, InAnimClass) : nullptr;
	if (InAnimClass && !NewInstance) return false;
	delete AnimScriptInstance;
	AnimClass = InAnimClass;
	AnimScriptInstance = NewInstance;
	if (AnimScriptInstance) AnimScriptInstance->InitializeAnimation();
	if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimScriptInstance))
	{
		if (SingleNode->SetAnimationAsset(AnimSequence) && AnimSequence) SingleNode->Play();
	}
	return true;
}

bool USkeletalMeshComponent::SetAnimGraphPath(const FString& InPath)
{
	if (!InPath.empty())
	{
		FAnimGraphAsset Graph;
		FString Error;
		if (!FAnimGraphAsset::Load(InPath, Graph, Error)) return false;
		if (SkeletalMesh && SkeletalMesh->Skeleton && !Graph.SkeletonPath.empty() &&
			Graph.SkeletonPath != SkeletalMesh->Skeleton->GetPath()) return false;
	}
	const FString PreviousPath = AnimGraphPath;
	const bool bReuseInstance = AnimClass == UAnimGraphInstance::StaticClass() && AnimScriptInstance;
	AnimGraphPath = InPath;
	if (InPath.empty())
	{
		if (AnimClass == UAnimGraphInstance::StaticClass()) SetAnimInstanceClass(nullptr);
		return true;
	}
	if (!SetAnimInstanceClass(UAnimGraphInstance::StaticClass())) { AnimGraphPath = PreviousPath; return false; }
	if (bReuseInstance && AnimScriptInstance) AnimScriptInstance->InitializeAnimation();
	return true;
}

bool USkeletalMeshComponent::SetAnimationSequence(UAnimSequence* InSequence)
{
	if (InSequence && (!SkeletalMesh || !SkeletalMesh->Skeleton ||
		InSequence->Skeleton != SkeletalMesh->Skeleton || !InSequence->GetDataModel())) return false;
	if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimScriptInstance))
	{
		if (!SingleNode->SetAnimationAsset(InSequence)) return false;
		if (InSequence) SingleNode->Play();
	}
	AnimSequence = InSequence;
	return true;
}

void USkeletalMeshComponent::EnsureBoneState()
{
	if (BoneStateMesh != SkeletalMesh) InitBoneTransforms();
}

void USkeletalMeshComponent::InitBoneTransforms()
{
	BoneStateMesh = SkeletalMesh;
	SkinnedVertexBuffer.reset();
	SkinnedLODs.Reset();
	ComputeSkinnedLODs.Reset();
	++PoseRevision;
	SkinningMatricesBuffer.reset();
	LocalBoneTransforms.Reset();
	GlobalBoneTransforms.Reset();
	SkinningMatrices.Reset();
	SkinnedVertices.Reset();
	bSkinningMatricesDirty = false;
	bGlobalBoneTransformsOverridden = false;
	AppliedMode = ESkinningMode::CPU;
	if (!SkeletalMesh) return;
	SkinnedLODs.SetNum(SkeletalMesh->AdditionalLODs.Num());

	const TArray<FBoneInfo>& Bones = SkeletalMesh->MeshData.Bones;
	int32 BoneCount = Bones.Num();

	LocalBoneTransforms.SetNum(BoneCount);
	GlobalBoneTransforms.SetNum(BoneCount);
	SkinningMatrices.SetNum(BoneCount);
	SkinnedBounds = SkeletalMesh->MeshData.AABB;
	if (BoneCount == 0)
	{
		const TArray<FSkeletalVertex>& Vertices = SkeletalMesh->MeshData.SkinnedVertices;
		SkinnedVertices.SetNum(Vertices.Num());
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			SkinnedVertices[Index].Position = Vertices[Index].Position;
			SkinnedVertices[Index].Normal = Vertices[Index].Normal;
			SkinnedVertices[Index].Tangent = Vertices[Index].Tangent;
			SkinnedVertices[Index].UV = Vertices[Index].UV;
			SkinnedVertices[Index].Color = Vertices[Index].Color;
		}
		SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			static_cast<uint32>(Vertices.Num() * sizeof(FVertex)), sizeof(FVertex));
		if (SkinnedVertexBuffer)
			RenderCommand::UpdateBufferData(SkinnedVertexBuffer.get(), SkinnedVertices.GetData(),
				static_cast<uint32>(Vertices.Num() * sizeof(FVertex)));
		return;
	}
	for (int32 i = 0; i < BoneCount; ++i)
	{
		LocalBoneTransforms[i] = Bones[i].LocalBindPose;
		GlobalBoneTransforms[i] = Bones[i].GlobalBindPose;
		SkinningMatrices[i] = FMatrix::Identity; // 초기 상태는 T-Pose
	}

	bSkinningMatricesDirty = true;
	UpdateSkinning();
}

void USkeletalMeshComponent::SetBoneTransform(int32 BoneIndex, const FMatrix& LocalTransform)
{
	if (BoneIndex < 0 || BoneIndex >= LocalBoneTransforms.Num())
	{
		return;
	}

	if (bGlobalBoneTransformsOverridden)
	{
		const TArray<FBoneInfo>& Bones = SkeletalMesh->MeshData.Bones;
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const int32 Parent = Bones[Index].ParentIndex;
			LocalBoneTransforms[Index] = Parent < 0 ? GlobalBoneTransforms[Index] :
				GlobalBoneTransforms[Index] * GlobalBoneTransforms[Parent].Inverse();
		}
		bGlobalBoneTransformsOverridden = false;
	}
	LocalBoneTransforms[BoneIndex] = LocalTransform;
	bSkinningMatricesDirty = true;
}

void USkeletalMeshComponent::SetGlobalBoneTransforms(const TArray<FMatrix>& InGlobalTransforms)
{
	if (!SkeletalMesh || InGlobalTransforms.Num() != GlobalBoneTransforms.Num()) return;
	GlobalBoneTransforms = InGlobalTransforms;
	bGlobalBoneTransformsOverridden = true;
	bSkinningMatricesDirty = true;
}

void USkeletalMeshComponent::SetBoneWeightHeatmap(bool bEnable, int32 BoneIndex)
{
	if (bBoneWeightHeatmap == bEnable && HeatmapBone == BoneIndex) return;
	// 꺼진 채로 본만 바뀌면 색이 그대로이므로 다시 스키닝하지 않는다.
	if (bBoneWeightHeatmap || bEnable) bVertexColorsDirty = true;
	bBoneWeightHeatmap = bEnable;
	HeatmapBone = BoneIndex;
}

void USkeletalMeshComponent::UpdateBoneMatrices()
{
	if (!SkeletalMesh) return;
	const auto& Bones = SkeletalMesh->MeshData.Bones;
	int32 BoneCount = Bones.Num();
	if (BoneCount == 0) return;

	FSkinningFrameStats& Stats = FSkinningStats::Pending();
	FSkinningCpuScope Timer(Stats.BoneMatrixCycles);
	Stats.BoneMatrices += static_cast<uint64>(BoneCount);

	for (int32 i = 0; i < BoneCount; ++i)
	{
		if (!bGlobalBoneTransformsOverridden)
		{	
			int32 ParentIndex = Bones[i].ParentIndex;
			if (ParentIndex == -1)
			{
				// 루트 본의 경우, 로컬 변환을 그대로 글로벌 변환으로 사용
				GlobalBoneTransforms[i] = LocalBoneTransforms[i];
			}
			else
			{
				// 부모 본의 글로벌 변환과 로컬 변환을 곱하여 글로벌 변환 계산
				GlobalBoneTransforms[i] = LocalBoneTransforms[i] * GlobalBoneTransforms[ParentIndex];
			}
		}
		// 스키닝 매트릭스 계산
		SkinningMatrices[i] = Bones[i].InverseBindPose * GlobalBoneTransforms[i];
	}
	bSkinningMatricesDirty = false;
}

void USkeletalMeshComponent::UpdateSkinning()
{
	if (!SkeletalMesh || SkinningMatrices.IsEmpty()) return;
	++PoseRevision;
	const bool bPoseChanged = bSkinningMatricesDirty;
	if (bPoseChanged) UpdateBoneMatrices();
	const ESkinningMode DesiredMode = GetDesiredSkinningMode();
	if (DesiredMode != ESkinningMode::CPU)
	{
		if (!SkinningMatricesBuffer)
			SkinningMatricesBuffer = RenderCommand::CreateDynamicStructuredBuffer(
				static_cast<uint32>(SkinningMatrices.Num()), sizeof(FMatrix));
		if (SkinningMatricesBuffer && SkinningMatricesBuffer->GetBuffer() && SkinningMatricesBuffer->GetSRV()
			&& (DesiredMode != ESkinningMode::Compute || FRenderResourceManager::GetComputeShader("Resources/Shader/ComputeSkinningCS.hlsli", "mainCS")))
		{
			const uint32 MatrixBytes = static_cast<uint32>(SkinningMatrices.Num() * sizeof(FMatrix));
			FSkinningFrameStats& Stats = FSkinningStats::Pending();
			{
				FSkinningCpuScope Timer(Stats.UploadCycles);
				RenderCommand::UpdateBufferData(SkinningMatricesBuffer.get(), SkinningMatrices.GetData(), MatrixBytes);
			}
			++Stats.GpuSkinnedComponents;
			Stats.MatrixUploadBytes += MatrixBytes;
			const bool bUpdateBounds = bPoseChanged || AppliedMode == ESkinningMode::CPU;
			bVertexColorsDirty = false;
			AppliedMode = DesiredMode;
			if (bUpdateBounds)
			{
				SkinnedBounds = SkeletalMesh->CalcSkinnedBounds(SkinningMatrices);
				MarkTransformDirty();
			}
			return;
		}
	}
	PerformCPUSkinning();
	AppliedMode = ESkinningMode::CPU;
}

FVertexBuffer* USkeletalMeshComponent::DispatchComputeSkinning(uint32 LOD)
{
	static_assert(sizeof(FVertex) == 64 && offsetof(FVertex, Normal) == 12
		&& offsetof(FVertex, Tangent) == 24 && offsetof(FVertex, UV) == 40 && offsetof(FVertex, Color) == 48);

	if (ComputeMeshRevision != SkeletalMesh->GetRenderDataRevision())
	{
		ComputeSkinnedLODs.Reset();
		ComputeMeshRevision = SkeletalMesh->GetRenderDataRevision();
	}
	ComputeSkinnedLODs.SetNum(SkeletalMesh->GetLODCount());
	auto& Cached = ComputeSkinnedLODs[LOD];
	if (Cached.Output && Cached.Revision == PoseRevision) return Cached.Output.get();

	FRenderDevice* RenderDevice = FRenderDevice::Get();
	if (!RenderDevice) return nullptr;
	ID3D11Device* Device = RenderDevice->GetDevice();
	const FStructuredBuffer* Source = SkeletalMesh->GetOrCreateComputeSkinningSource(LOD, Device);
	if (!Source) return nullptr;

	if (!Cached.Output)
	{
		D3D11_BUFFER_DESC Desc{};
		Desc.ByteWidth = Source->GetCapacity() * sizeof(FVertex);
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
		Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		auto Buffer = std::make_unique<FVertexBuffer>(Device, Desc, sizeof(FVertex));
		if (!Buffer->GetBuffer()) return nullptr;

		D3D11_UNORDERED_ACCESS_VIEW_DESC View{};
		View.Format = DXGI_FORMAT_R32_TYPELESS;
		View.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		View.Buffer.NumElements = Desc.ByteWidth / sizeof(uint32);
		View.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(Device->CreateUnorderedAccessView(Buffer->GetBuffer(), &View, Cached.OutputUAV.ReleaseAndGetAddressOf())))
			return nullptr;
		Cached.Output = std::move(Buffer);
	}
	ID3D11DeviceContext* Context = RenderDevice->GetContext();

	// The preceding draw may still have this output bound at IA slot 0.
	RenderCommand::BindVertexBuffer(nullptr);
	ID3D11UnorderedAccessView* Output = Cached.OutputUAV.Get();
	Context->CSSetUnorderedAccessViews(0, 1, &Output, nullptr);
	RenderCommand::BindComputeShader(FRenderResourceManager::GetComputeShader("Resources/Shader/ComputeSkinningCS.hlsli", "mainCS"));
	RenderCommand::BindShaderResource(0, Source, EShaderBindFlagBits::Compute);
	RenderCommand::BindShaderResource(16, SkinningMatricesBuffer.get(), EShaderBindFlagBits::Compute);
	const uint32 Groups = (Source->GetCapacity() + 63) / 64;
	constexpr uint32 GroupsPerRow = 32768; // Must match ComputeSkinningCS.hlsli.
	FGpuProfiler& Profiler = FGpuProfiler::Get();
	const int32 TimingHandle = FSkinningStats::IsCollecting() ? Profiler.BeginSkinningDispatch() : -1;
	RenderCommand::Dispatch(std::min(Groups, GroupsPerRow), (Groups + GroupsPerRow - 1) / GroupsPerRow, 1);
	Profiler.EndScope(TimingHandle);

	ID3D11UnorderedAccessView* NullUAV = nullptr;
	Context->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
	RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Compute);
	RenderCommand::UnbindShaderResources(16, 1, EShaderBindFlagBits::Compute);
	RenderCommand::BindComputeShader(nullptr);
	Cached.Revision = PoseRevision;
	return Cached.Output.get();
}

void USkeletalMeshComponent::PerformCPUSkinning()
{
	if (!SkeletalMesh) return;
	const FSkeletalMeshData& MeshData = SkeletalMesh->MeshData;
	const int32 BoneCount = MeshData.Bones.Num();
	if (BoneCount == 0) return;
	if (bSkinningMatricesDirty) UpdateBoneMatrices();
	if (!SkinnedVertexBuffer)
		SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			static_cast<uint32>(MeshData.SkinnedVertices.Num() * sizeof(FVertex)), sizeof(FVertex));

	int32 VertexCount = static_cast<int32>(MeshData.SkinnedVertices.Num());
	SkinnedVertices.SetNum(VertexCount);

	FVector BoundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector BoundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	FSkinningFrameStats& Stats = FSkinningStats::Pending();
	++Stats.CpuSkinnedComponents;
	Stats.CpuSkinnedVertices += static_cast<uint64>(VertexCount);
	std::optional<FSkinningCpuScope> SkinTimer(std::in_place, Stats.CpuSkinCycles);

	for (int32 v = 0; v < VertexCount; ++v)
	{
		const FSkeletalVertex& Src = MeshData.SkinnedVertices[v];
		FVertex& Dst = SkinnedVertices[v];

		// Bone Influence(최대 4개)의 스키닝 행렬을 가중치로 섞어 행렬 하나로 만든다
		float B[4][3] = {};
		float TotalWeight = 0.0f;
		for (int32 i = 0; i < 4; ++i)
		{
			int32 BoneIndex = Src.BoneIndices[i];
			float Weight = Src.BoneWeights[i];
			
			if (BoneIndex < 0 || BoneIndex >= BoneCount || Weight <= 0.0f)
				continue;

			const FMatrix& S = SkinningMatrices[BoneIndex];
			for (int32 Row = 0; Row < 4; ++Row)
			{
				B[Row][0] += S.M[Row][0] * Weight;
				B[Row][1] += S.M[Row][1] * Weight;
				B[Row][2] += S.M[Row][2] * Weight;
			}
			TotalWeight += Weight;
		}

		// Bone Influence가 없으면 단위 행렬 = 바인드 포즈 그대로
		if (TotalWeight <= 0.0f)
			B[0][0] = B[1][1] = B[2][2] = 1.0f;

		// 섞은 행렬 하나로 위치, 노멀, 탄젠트를 변환
		TransformPoint(B, Src.Position, Dst.Position);
		TransformDirection(B, Src.Normal.X, Src.Normal.Y, Src.Normal.Z, Dst.Normal.V);
		TransformDirection(B, Src.Tangent.X, Src.Tangent.Y, Src.Tangent.Z, Dst.Tangent.V);
		Dst.Tangent.W = Src.Tangent.W;
		Dst.UV = Src.UV;
		Dst.Color = bBoneWeightHeatmap ? WeightToHeatColor(GetBoneWeight(Src, HeatmapBone)) : Src.Color;

		BoundsMin.X = std::min(BoundsMin.X, Dst.Position.X);
		BoundsMin.Y = std::min(BoundsMin.Y, Dst.Position.Y);
		BoundsMin.Z = std::min(BoundsMin.Z, Dst.Position.Z);
		BoundsMax.X = std::max(BoundsMax.X, Dst.Position.X);
		BoundsMax.Y = std::max(BoundsMax.Y, Dst.Position.Y);
		BoundsMax.Z = std::max(BoundsMax.Z, Dst.Position.Z);
	}

	SkinTimer.reset();   // 정점 변환까지만 CPU Skin 시간에 넣는다

	SkinnedBounds.Min = BoundsMin;
	SkinnedBounds.Max = BoundsMax;
	MarkTransformDirty();

	if (SkinnedVertexBuffer)
	{
		FSkinningCpuScope UploadTimer(Stats.UploadCycles);
		RenderCommand::UpdateBufferData(
			SkinnedVertexBuffer.get(),
			SkinnedVertices.GetData(),
			VertexCount * sizeof(FVertex)
		);
		Stats.VertexUploadBytes += static_cast<uint64>(VertexCount) * sizeof(FVertex);
	}

	bVertexColorsDirty = false;
	AppliedMode = ESkinningMode::CPU;
}

void USkeletalMeshComponent::EndSkinningStatsFrame()
{
	FSkinningStats::NotifyModeChanged(SkinningMode);
	const bool bCollecting = FSkinningStats::IsCollecting();
	if (!bCollecting && !bForceSkinningUpdate)
	{
		FSkinningStats::EndFrame({});
		return;
	}

	// 에셋 미리보기 창의 컴포넌트도 실제로 스키닝하므로 함께 센다.
	FSkinningFrameStats Counts{};
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Component = *It;
		if (!Component || !Component->IsRegistered() || !Component->SkeletalMesh) continue;
		// 다음 프레임의 Tick/렌더 수집이 스키닝을 다시 하도록 더럽혀 둔다 (프레임당 한 번).
		if (bForceSkinningUpdate && !Component->SkinningMatrices.IsEmpty())
			Component->bSkinningMatricesDirty = true;
		if (!bCollecting) continue;
		++Counts.TotalComponents;
		if (Component->AppliedMode != ESkinningMode::CPU) ++Counts.GpuModeComponents;
		else ++Counts.CpuModeComponents;
		Counts.TotalVertices += static_cast<uint64>(Component->SkeletalMesh->MeshData.SkinnedVertices.Num());
		Counts.TotalBones += static_cast<uint64>(Component->SkeletalMesh->MeshData.Bones.Num());
	}
	FSkinningStats::EndFrame(bCollecting ? Counts : FSkinningFrameStats{});
}

FBox USkeletalMeshComponent::CalcLocalBounds() const
{
	const_cast<USkeletalMeshComponent*>(this)->EnsureBoneState();
	if (SkeletalMesh && NeedsSkinningUpdate())
		const_cast<USkeletalMeshComponent*>(this)->UpdateSkinning();
	return SkeletalMesh ? SkinnedBounds : Super::CalcLocalBounds();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

FVertexBuffer* USkeletalMeshComponent::GetSkinnedVertexBuffer(uint32 LOD)
{
	EnsureBoneState();
	if (!SkeletalMesh || LOD >= SkeletalMesh->GetLODCount()) return nullptr;
	if (NeedsSkinningUpdate()) UpdateSkinning();
	if (AppliedMode == ESkinningMode::Compute)
	{
		if (SkeletalMesh->GetMeshData(LOD).SkinnedVertices.IsEmpty()) return nullptr;
		if (FVertexBuffer* Output = DispatchComputeSkinning(LOD)) return Output;
		PerformCPUSkinning();
	}
	if (AppliedMode == ESkinningMode::GPU) return nullptr;
	if (LOD == 0) return SkinnedVertexBuffer.get();
	FSkinnedLOD& Cached = SkinnedLODs[LOD - 1];
	if (Cached.Buffer && Cached.Revision == PoseRevision) return Cached.Buffer.get();
	const auto& Data = SkeletalMesh->GetMeshData(LOD);
	Cached.Vertices.SetNum(Data.SkinnedVertices.Num());
	FSkinningFrameStats& Stats = FSkinningStats::Pending();
	Stats.CpuSkinnedVertices += static_cast<uint64>(Data.SkinnedVertices.Num());
	std::optional<FSkinningCpuScope> SkinTimer(std::in_place, Stats.CpuSkinCycles);
	for (int32 V = 0; V < Data.SkinnedVertices.Num(); ++V)
	{
		const auto& Src = Data.SkinnedVertices[V]; auto& Dst = Cached.Vertices[V];
		float Blend[4][3]{}; float Total = 0;
		for (int I = 0; I < 4; ++I)
		{
			const int Bone = Src.BoneIndices[I]; const float Weight = Src.BoneWeights[I];
			if (Bone < 0 || Bone >= SkinningMatrices.Num() || Weight <= 0) continue;
			for (int R = 0; R < 4; ++R) for (int C = 0; C < 3; ++C) Blend[R][C] += SkinningMatrices[Bone].M[R][C] * Weight;
			Total += Weight;
		}
		if (Total <= 0) Blend[0][0] = Blend[1][1] = Blend[2][2] = 1;
		TransformPoint(Blend, Src.Position, Dst.Position);
		TransformDirection(Blend, Src.Normal.X, Src.Normal.Y, Src.Normal.Z, Dst.Normal.V);
		TransformDirection(Blend, Src.Tangent.X, Src.Tangent.Y, Src.Tangent.Z, Dst.Tangent.V);
		Dst.Tangent.W = Src.Tangent.W; Dst.UV = Src.UV;
		Dst.Color = bBoneWeightHeatmap ? WeightToHeatColor(GetBoneWeight(Src, HeatmapBone)) : Src.Color;
	}
	SkinTimer.reset();
	if (!Cached.Buffer) Cached.Buffer = RenderCommand::CreateDynamicVertexBuffer(static_cast<uint32>(Cached.Vertices.Num() * sizeof(FVertex)), sizeof(FVertex));
	if (Cached.Buffer)
	{
		const uint32 VertexBytes = static_cast<uint32>(Cached.Vertices.Num() * sizeof(FVertex));
		FSkinningCpuScope UploadTimer(Stats.UploadCycles);
		RenderCommand::UpdateBufferData(Cached.Buffer.get(), Cached.Vertices.GetData(), VertexBytes);
		Stats.VertexUploadBytes += VertexBytes;
	}
	Cached.Revision = PoseRevision;
	return Cached.Buffer.get();
}

void USkeletalMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	EnsureBoneState();
	if (!SkeletalMesh) return;
	if (NeedsSkinningUpdate()) UpdateSkinning();
	if (!SkinnedVertexBuffer && AppliedMode == ESkinningMode::CPU) return;
	FMeshRenderState State;
	State.LODCount = static_cast<uint8>(SkeletalMesh->GetLODCount());
	for (int I = 0; I < 3; ++I) State.LODThresholdSq[I] = SkeletalMesh->ScreenThresholds[I] * SkeletalMesh->ScreenThresholds[I];
	const FLODSelectionInput Input{ SkeletalMesh->GetLocalLODSphere().Transform(GetWorldMatrix()), &State };
	const uint32 LOD = ViewContext.bOrthographic ? SelectSphereLOD<true>(Input, ViewContext) : SelectSphereLOD<false>(Input, ViewContext);
	const FSkeletalMeshData& MeshData = SkeletalMesh->GetMeshData(LOD);
	FVertexBuffer* VertexBuffer = AppliedMode != ESkinningMode::GPU ? GetSkinnedVertexBuffer(LOD) : nullptr;
	if (AppliedMode != ESkinningMode::GPU && !VertexBuffer) return;
	// 히트맵은 텍스처 없는 흰 머티리얼로 모든 섹션을 덮는다. 그래야 UberLit이 정점 색(히트 색)을 표면 색으로 쓴다.
	UMaterial* HeatmapMaterial = bBoneWeightHeatmap ? UAssetManager::GetAssetByPath<UMaterial>("BoneWeightHeatmapMaterial") : nullptr;

	for (const FSkeletalMeshSection& Section : MeshData.Sections)
	{
		UMaterial* SectionMaterial = HeatmapMaterial ? HeatmapMaterial : GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!SectionMaterial) continue;

		FRenderPacket Packet;
		Packet.SkeletalMesh = SkeletalMesh;
		Packet.SkeletalVertexBuffer = VertexBuffer;
		Packet.LODIndex = static_cast<uint8>(LOD);
		Packet.SkinningMatricesBuffer = AppliedMode == ESkinningMode::GPU ? SkinningMatricesBuffer.get() : nullptr;
		Packet.bComputeSkinned = AppliedMode == ESkinningMode::Compute;
		Packet.Model = RenderQueue.StoreWorldMatrix(GetWorldMatrix());
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.Material = SectionMaterial;
		RenderQueue.Add(Packet);
	}
}

void USkeletalMeshComponent::UpdatePickPositions()
{
	EnsureBoneState();
	if (!SkeletalMesh) return;
	if (!SkinningMatrices.IsEmpty() && bSkinningMatricesDirty) UpdateSkinning();
	if (PickRevision == PoseRevision && !PickPositions.IsEmpty()) return;

	const TArray<FSkeletalVertex>& Vertices = SkeletalMesh->MeshData.SkinnedVertices;
	PickPositions.SetNum(Vertices.Num());
	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX), Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (int32 V = 0; V < Vertices.Num(); ++V)
	{
		const FSkeletalVertex& Src = Vertices[V];
		FBlendMatrix Blend{};
		float Total = 0.0f;
		for (int32 I = 0; I < 4; ++I)
		{
			const int32 Bone = Src.BoneIndices[I];
			const float Weight = Src.BoneWeights[I];
			if (Bone < 0 || Bone >= SkinningMatrices.Num() || Weight <= 0.0f) continue;
			for (int32 R = 0; R < 4; ++R) for (int32 C = 0; C < 3; ++C) Blend[R][C] += SkinningMatrices[Bone].M[R][C] * Weight;
			Total += Weight;
		}
		if (Total <= 0.0f) Blend[0][0] = Blend[1][1] = Blend[2][2] = 1.0f;
		FVector& P = PickPositions[V];
		TransformPoint(Blend, Src.Position, P);
		Min = FVector(std::min(Min.X, P.X), std::min(Min.Y, P.Y), std::min(Min.Z, P.Z));
		Max = FVector(std::max(Max.X, P.X), std::max(Max.Y, P.Y), std::max(Max.Z, P.Z));
	}
	PickBounds = FBox{ Min, Max };
	PickRevision = PoseRevision;
}

bool USkeletalMeshComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FMatrix WorldToLocal = GetWorldMatrix().Inverse();
	const FRay LocalRay{ .Origin = WorldToLocal.TransformPosition(WorldRay.Origin), .Direction = WorldToLocal.TransformVector(WorldRay.Direction) };
	float T = FLT_MAX;
	if (!LineTraceComponentLocal(LocalRay, T)) return false;
	OutHit.bBlockingHit = true;
	OutHit.Distance = T;
	OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
	OutHit.HitComponent = this;
	return true;
}

bool USkeletalMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	UpdatePickPositions();
	if (!SkeletalMesh || PickPositions.IsEmpty()) return false;

	// 포즈를 반영한 박스로 먼저 거른다. 빈 공간을 클릭해도 메시가 선택되지 않는다.
	float BoxT = 0.0f;
	if (!RayIntersectsAABB(LocalRay, PickBounds.Min, PickBounds.Max, BoxT) || BoxT >= OutT) return false;

	const TArray<uint32>& Indices = SkeletalMesh->MeshData.Indices;
	bool bHit = false;
	for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
	{
		float T = 0.0f;
		if (RayIntersectsTriangle(LocalRay, PickPositions[Indices[I]], PickPositions[Indices[I + 1]], PickPositions[Indices[I + 2]], T) && T < OutT)
		{
			OutT = T;
			bHit = true;
		}
	}
	return bHit;
}
