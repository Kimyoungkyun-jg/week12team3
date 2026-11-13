#include "EnginePCH.h"
#include "FbxAssetImporter.h"
#include "FbxAssetBake.h"
#include "FbxPath.h"
#include "Animation/Skeleton.h"
#include "Asset/AssetManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifndef WITH_FBX_SDK
#define WITH_FBX_SDK 1
#endif

#if WITH_FBX_SDK
#include <fbxsdk.h>

namespace
{
FString Lower(FString S)
{
	for (char& C : S)
	{
		C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
	}
	return S;
}

FString NodePath(FbxNode* Node)
{
	if (!Node || !Node->GetParent())
	{
		return "/";
	}
	FString Name = Node->GetName();
	int Occurrence = 0;
	for (int I = 0; I < Node->GetParent()->GetChildCount(); ++I)
	{
		FbxNode* Sibling = Node->GetParent()->GetChild(I);
		if (Sibling == Node)
		{
			break;
		}
		if (Name == Sibling->GetName())
		{
			++Occurrence;
		}
	}
	return NodePath(Node->GetParent()) + Name + "[" + std::to_string(Occurrence) + "]/";
}

bool HasAttribute(FbxNode* Node, FbxNodeAttribute::EType Type)
{
	for (int I = 0; Node && I < Node->GetNodeAttributeCount(); ++I)
	{
		if (Node->GetNodeAttributeByIndex(I)->GetAttributeType() == Type)
		{
			return true;
		}
	}
	return false;
}

bool IsCompensation(FbxCluster* Cluster)
{
	return Cluster && !std::strcmp(Cluster->GetUserDataID(), "Maya_ClusterHint") &&
		!std::strcmp(Cluster->GetUserData(), "CompensationCluster");
}

// UE FFbxScene::Internal_GetRootSkeleton의 상향 경계와 Blender Armature 예외를 따른다.
FbxNode* FindRoot(FbxScene* Scene, FbxNode* Link, bool bBlender)
{
	FbxNode* Root = Link;
	while (Root && Root->GetParent() && Root->GetParent() != Scene->GetRootNode())
	{
		FbxNode* Parent = Root->GetParent();
		FbxNodeAttribute* Attribute = Parent->GetNodeAttribute();
		if (!Attribute)
		{
			break;
		}
		const auto Type = Attribute->GetAttributeType();
		const bool bArmature = bBlender && Lower(Parent->GetName()) == "armature" &&
			(!Parent->GetParent() || Parent->GetParent() == Scene->GetRootNode());
		if (Type != FbxNodeAttribute::eSkeleton && Type != FbxNodeAttribute::eMesh &&
			(Type != FbxNodeAttribute::eNull || bArmature))
		{
			break;
		}
		if (Type == FbxNodeAttribute::eMesh && static_cast<FbxMesh*>(Attribute)->GetDeformerCount(FbxDeformer::eSkin))
		{
			break;
		}
		Root = Parent;
	}
	return Root;
}

bool DescendsFrom(FbxNode* Node, FbxNode* Root)
{
	for (; Node; Node = Node->GetParent())
	{
		if (Node == Root)
		{
			return true;
		}
	}
	return false;
}

// 어느 스택에서든 노드의 TRS 값이 실제로 변하면 참이다. 일정한 값으로 구운 커브는 움직임이 아니다.
bool HasTransformAnimation(FbxScene* Scene, FbxNode* Node)
{
	constexpr float Tolerance = 1.e-4f;
	const char* Channels[]{ FBXSDK_CURVENODE_COMPONENT_X, FBXSDK_CURVENODE_COMPONENT_Y, FBXSDK_CURVENODE_COMPONENT_Z };
	FbxProperty Properties[]{ Node->LclTranslation, Node->LclRotation, Node->LclScaling };
	for (int S = 0; S < Scene->GetSrcObjectCount<FbxAnimStack>(); ++S)
	{
		auto* Stack = Scene->GetSrcObject<FbxAnimStack>(S);
		for (int L = 0; L < Stack->GetMemberCount<FbxAnimLayer>(); ++L)
		{
			auto* Layer = Stack->GetMember<FbxAnimLayer>(L);
			for (FbxProperty& Property : Properties)
			{
				for (const char* Channel : Channels)
				{
					FbxAnimCurve* Curve = Property.GetCurve(Layer, Channel);
					if (!Curve || Curve->KeyGetCount() < 2)
					{
						continue;
					}
					float Min = Curve->KeyGetValue(0), Max = Min;
					for (int K = 1; K < Curve->KeyGetCount(); ++K)
					{
						const float Value = Curve->KeyGetValue(K);
						Min = std::min(Min, Value);
						Max = std::max(Max, Value);
					}
					if (Max - Min > Tolerance)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

// 부모가 움직여도 메시는 따라 움직인다.
bool MovesWithAnimation(FbxScene* Scene, FbxNode* Node)
{
	for (; Node && Node != Scene->GetRootNode(); Node = Node->GetParent())
	{
		if (HasTransformAnimation(Scene, Node))
		{
			return true;
		}
	}
	return false;
}

bool CollisionName(FString Name)
{
	Name = Lower(Name);
	// 후속 충돌 임포트는 이 분류에서 별도 데이터 경로로 연결한다.
	for (const char* Prefix : { "ucx_", "ubx_", "usp_", "ucp_", "mcdcX_" })
	{
		if (Name.starts_with(Lower(Prefix)))
		{
			return true;
		}
	}
	return false;
}

FbxAMatrix Geometry(FbxNode* Node)
{
	FbxAMatrix Out;
	Out.SetT(Node->GetGeometricTranslation(FbxNode::eSourcePivot));
	Out.SetR(Node->GetGeometricRotation(FbxNode::eSourcePivot));
	Out.SetS(Node->GetGeometricScaling(FbxNode::eSourcePivot));
	return Out;
}

FVector Reflect(const FbxVector4& V) { return FVector(static_cast<float>(V[0]), static_cast<float>(-V[1]), static_cast<float>(V[2])); }

FMatrix EngineMatrix(const FbxAMatrix& M, double UnitScale, bool bEngineInput = true)
{
	const FbxVector4 O = M.MultT(FbxVector4(0, 0, 0));
	const FVector X = Reflect(M.MultT(FbxVector4(1, 0, 0)) - O);
	const FVector Y = Reflect(M.MultT(FbxVector4(0, 1, 0)) - O) * (bEngineInput ? -1.f : 1.f);
	const FVector Z = Reflect(M.MultT(FbxVector4(0, 0, 1)) - O);
	const FVector T = Reflect(O) * static_cast<float>(UnitScale);
	return FMatrix(X.X, X.Y, X.Z, 0, Y.X, Y.Y, Y.Z, 0, Z.X, Z.Y, Z.Z, 0, T.X, T.Y, T.Z, 1);
}

// FBX 로컬 행렬을 엔진 위치·회전·크기로 바꾼다. EngineMatrix와 같은 축 변환(Y 반사)을 성분에 적용한 것이다.
// 바인드 포즈(BuildRig)와 애니메이션 키(CookAnimation)가 함께 써야 두 값이 같은 공간에 놓인다.
FBoneTransform EngineBoneTransform(const FbxAMatrix& Local, double UnitScale)
{
	const FbxQuaternion Q = Local.GetQ();
	const FbxVector4 S = Local.GetS();
	return FBoneTransform(Reflect(Local.GetT()) * static_cast<float>(UnitScale),
		FQuat(static_cast<float>(Q[0]), static_cast<float>(-Q[1]), static_cast<float>(Q[2]), static_cast<float>(-Q[3])).Normalized(),
		FVector(static_cast<float>(S[0]), static_cast<float>(S[1]), static_cast<float>(S[2])));
}

bool FiniteMatrix(const FMatrix& M)
{
	for (int R = 0; R < 4; ++R)
	{
		for (int C = 0; C < 4; ++C)
		{
			if (!std::isfinite(M.M[R][C]))
			{
				return false;
			}
		}
	}
	return std::abs(M.Determinant()) > 1.e-12f;
}

bool Near(const FbxAMatrix& A, const FbxAMatrix& B)
{
	for (int R = 0; R < 4; ++R)
	{
		for (int C = 0; C < 4; ++C)
		{
			if (std::abs(A.Get(R, C) - B.Get(R, C)) > 1.e-4 * (1 + std::abs(A.Get(R, C))))
			{
				return false;
			}
		}
	}
	return true;
}

// 소켓에 바로 꽂힌 파일이 없으면 LayeredTexture의 첫 레이어를 쓴다. 엔진은 슬롯당 한 장만 받는다.
FbxFileTexture* PropertyTexture(const FbxProperty& Property)
{
	if (!Property.IsValid())
	{
		return nullptr;
	}
	if (auto* File = Property.GetSrcObject<FbxFileTexture>(0))
	{
		return File;
	}
	for (int I = 0; I < Property.GetSrcObjectCount<FbxLayeredTexture>(); ++I)
	{
		if (auto* File = Property.GetSrcObject<FbxLayeredTexture>(I)->GetSrcObject<FbxFileTexture>(0))
		{
			return File;
		}
	}
	return nullptr;
}

// PBR 머티리얼의 BaseColor와 Bump에 꽂힌 노멀맵은 표준 소켓이 비었을 때 대신 읽는다.
FbxProperty MaterialInput(FbxSurfaceMaterial* Material, const char* PropertyName)
{
	if (!Material)
	{
		return {};
	}
	const FbxProperty Property = Material->FindProperty(PropertyName);
	if (PropertyTexture(Property))
	{
		return Property;
	}
	const char* Fallback = !std::strcmp(PropertyName, FbxSurfaceMaterial::sDiffuse) ? "BaseColor" : !std::strcmp(PropertyName, FbxSurfaceMaterial::sNormalMap) ? FbxSurfaceMaterial::sBump
																																							   : nullptr;
	if (Fallback)
	{
		const FbxProperty Alternative = Material->FindProperty(Fallback);
		if (PropertyTexture(Alternative))
		{
			return Alternative;
		}
	}
	return Property;
}

FbxFileTexture* FileTexture(FbxSurfaceMaterial* Material, const char* PropertyName)
{
	return PropertyTexture(MaterialInput(Material, PropertyName));
}

// FBX UV에 적용하는 2D affine 변환. 엔진의 V 반전은 이 변환 이후에 수행한다.
struct FUVTransform
{
	double M[2][3]{ { 1, 0, 0 }, { 0, 1, 0 } };
	bool bValid = true;
	FbxVector2 Apply(const FbxVector2& UV) const
	{
		return { M[0][0] * UV[0] + M[0][1] * UV[1] + M[0][2], M[1][0] * UV[0] + M[1][1] * UV[1] + M[1][2] };
	}
};

FUVTransform UVTransform(FbxFileTexture* Texture)
{
	FUVTransform Out;
	if (!Texture)
	{
		return Out;
	}
	FbxAMatrix Scale, Rotation, Translation, ScalePivot, InvScalePivot, RotationPivot, InvRotationPivot;
	Scale.SetS(FbxVector4(Texture->GetScaleU(), Texture->GetScaleV(), 1));
	Rotation.SetR(FbxVector4(Texture->GetRotationU(), Texture->GetRotationV(), Texture->GetRotationW()));
	Translation.SetT(FbxVector4(Texture->GetTranslationU(), Texture->GetTranslationV(), 0));
	const FbxDouble3 SP = Texture->ScalingPivot.Get(), RP = Texture->RotationPivot.Get();
	ScalePivot.SetT(FbxVector4(SP[0], SP[1], SP[2]));
	InvScalePivot.SetT(FbxVector4(-SP[0], -SP[1], -SP[2]));
	RotationPivot.SetT(FbxVector4(RP[0], RP[1], RP[2]));
	InvRotationPivot.SetT(FbxVector4(-RP[0], -RP[1], -RP[2]));
	const FbxAMatrix Matrix = Translation * RotationPivot * Rotation * InvRotationPivot * ScalePivot * Scale * InvScalePivot;
	FbxVector4 O = Matrix.MultT(FbxVector4(0, 0, 0)), U = Matrix.MultT(FbxVector4(1, 0, 0)), V = Matrix.MultT(FbxVector4(0, 1, 0));
	if (Texture->GetSwapUV())
	{
		std::swap(O[0], O[1]);
		std::swap(U[0], U[1]);
		std::swap(V[0], V[1]);
	}
	for (int I = 0; I < 2; ++I)
	{
		Out.M[I][0] = U[I] - O[I];
		Out.M[I][1] = V[I] - O[I];
		Out.M[I][2] = O[I];
		for (double Value : Out.M[I])
		{
			Out.bValid &= std::isfinite(Value);
		}
	}
	Out.bValid &= Texture->GetMappingType() == FbxTexture::eUV;
	return Out;
}

bool Near(const FUVTransform& A, const FUVTransform& B)
{
	for (int R = 0; R < 2; ++R)
	{
		for (int C = 0; C < 3; ++C)
		{
			if (std::abs(A.M[R][C] - B.M[R][C]) > 1.e-6 * (1 + std::max(std::abs(A.M[R][C]), std::abs(B.M[R][C]))))
			{
				return false;
			}
		}
	}
	return true;
}

struct FSurfaceUV
{
	FString Set;
	FUVTransform Transform;
	FbxGeometryElementTangent* Tangents = nullptr;
	FbxGeometryElementBinormal* Binormals = nullptr;
};

template<class T>
bool CornerVector(T* Element, FbxMesh* Mesh, int Polygon, int Corner, FbxVector4& Out)
{
	if (!Element)
	{
		return false;
	}
	int Index;
	switch (Element->GetMappingMode())
	{
	case FbxLayerElement::eByControlPoint:
		Index = Mesh->GetPolygonVertex(Polygon, Corner);
		break;
	case FbxLayerElement::eByPolygonVertex:
		Index = Mesh->GetPolygonVertexIndex(Polygon) + Corner;
		break;
	case FbxLayerElement::eByPolygon:
		Index = Polygon;
		break;
	case FbxLayerElement::eAllSame:
		Index = 0;
		break;
	default:
		return false;
	}
	if (Element->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
	{
		if (Index < 0 || Index >= Element->GetIndexArray().GetCount())
		{
			return false;
		}
		Index = Element->GetIndexArray().GetAt(Index);
	}
	if (Index < 0 || Index >= Element->GetDirectArray().GetCount())
	{
		return false;
	}
	Out = Element->GetDirectArray().GetAt(Index);
	return std::isfinite(Out[0]) && std::isfinite(Out[1]) && std::isfinite(Out[2]);
}

bool ImportTangent(const FSurfaceUV& UV, FbxMesh* Mesh, int Polygon, int Corner, const FMatrix& GeometryTransform, FVertex& Vertex)
{
	FbxVector4 T, B;
	if (!CornerVector(UV.Tangents, Mesh, Polygon, Corner, T))
	{
		return false;
	}
	if (!CornerVector(UV.Binormals, Mesh, Polygon, Corner, B))
	{
		FbxVector4 N;
		if (!Mesh->GetPolygonVertexNormal(Polygon, Corner, N) || !std::isfinite(T[3]) || std::abs(std::abs(T[3]) - 1) > 1.e-4)
		{
			return false;
		}
		B = N.CrossProduct(T) * T[3];
	}
	auto Direction = [&](const FbxVector4& V)
	{ return GeometryTransform.TransformVector(FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]))); };
	const FVector GT = Direction(T), GB = Direction(B);
	// 최종 UV의 Jacobian 역행렬로 원본 T/B를 옮긴다. 두 번째 행의 부호는 1-V 변환이다.
	const auto& M = UV.Transform.M;
	const double J00 = M[0][0], J01 = M[0][1], J10 = -M[1][0], J11 = -M[1][1];
	const double Det = J00 * J11 - J01 * J10;
	if (!std::isfinite(Det) || std::abs(Det) < 1.e-12)
	{
		return false;
	}
	FVector Tangent = GT * static_cast<float>(J11 / Det) + GB * static_cast<float>(-J10 / Det);
	const FVector Bitangent = GT * static_cast<float>(-J01 / Det) + GB * static_cast<float>(J00 / Det);
	Tangent = (Tangent - Vertex.Normal * Vertex.Normal.Dot(Tangent)).GetSafeNormal();
	const float Orientation = Vertex.Normal.Cross(Tangent).Dot(Bitangent);
	if (!std::isfinite(Orientation) || std::abs(Orientation) < 1.e-8f || Tangent.SizeSquared() < 1.e-8f)
	{
		return false;
	}
	Vertex.Tangent = FVector4(Tangent, Orientation < 0 ? -1.f : 1.f);
	return true;
}

struct FMeshItem
{
	FbxNode* Node = nullptr;
	FbxMesh* Mesh = nullptr;
	FbxNode* Rig = nullptr;
	FbxNode* LODGroupNode = nullptr;
	FString UnitKey;
	FString Label;
	int LOD = -1;
};

struct FRig
{
	FbxNode* Root = nullptr;
	std::set<FbxNode*> Needed;
	std::vector<FbxNode*> Bones;
	std::unordered_map<FbxNode*, uint16> Indices;
	FReferenceSkeleton Reference;
	FbxAMatrix ParentBind;
	FbxAMatrix FrameInverse; // 본 글로벌을 리그 기준 공간으로 옮기는 행렬. Bake Meshes면 단위 행렬이다.
	// Target Skeleton 모드에서 본마다 1이면 대상 Skeleton에 같은 이름과 같은 부모로 있는 본이다. 비어 있으면 모든 본을 쓴다.
	std::vector<uint8> TargetMask;
	FString Key;
};

struct FOutputGroup
{
	FString Identity;
	FString Label;
	FbxNode* Root = nullptr;
	std::vector<FMeshItem*> Items;
};

struct FCornerKey
{
	int CP;
	int Surface;
	FVertex Vertex;
	bool operator==(const FCornerKey& Other) const { return CP == Other.CP && Surface == Other.Surface && !std::memcmp(&Vertex, &Other.Vertex, sizeof(Vertex)); }
};
struct FCornerHash
{
	size_t operator()(const FCornerKey& Key) const
	{
		uint64 Hash = static_cast<uint32>(Key.CP) ^ (static_cast<uint64>(Key.Surface) << 32);
		for (size_t I = 0; I < sizeof(FVertex); ++I)
		{
			Hash = (Hash ^ reinterpret_cast<const uint8*>(&Key.Vertex)[I]) * 1099511628211ull;
		}
		return Hash;
	}
};

class FImport
{
public:
	FbxScene* Scene;
	FFbxImportOptions Options;
	FString SourceKey;
	fs::path Source;
	fs::path Destination;
	FString ActualSourceKey;
	uint64 SourceHash = 0;
	double UnitScale = 1;
	bool bBlender = false;
	TArray<FFbxAssetData>& Output;
	TArray<FString>& Warnings;
	FString& Error;
	std::vector<FMeshItem> Meshes;
	std::map<FString, FRig> Rigs;
	std::unordered_map<FbxNode*, FbxAMatrix> ClusterBones;
	std::unordered_map<FbxMesh*, FbxAMatrix> ClusterMeshes;
	std::unordered_map<FbxNode*, FbxAMatrix> BindCache;
	std::unordered_map<FbxSurfaceMaterial*, FString> Materials;
	std::unordered_set<FbxSurfaceMaterial*> SkippedOpacityTextures;
	std::unordered_map<FString, FString> Textures;
	std::unordered_map<FString, FString> FileOwners;
	std::unordered_set<FString> Warned;
	std::vector<FbxPose*> Poses;
	// Target Skeleton 모드면 대상 Skeleton이다. 이때는 메시와 새 Skeleton 없이 애니메이션만 만든다.
	const USkeleton* TargetSkeleton = nullptr;

	FImport(FbxScene* S, const FFbxImportOptions& O, const FString& K, const fs::path& P, const fs::path& D,
		TArray<FFbxAssetData>& Out, TArray<FString>& W, FString& E)
		: Scene(S)
		, Options(O)
		, SourceKey(K)
		, Source(P)
		, Destination(D)
		, Output(Out)
		, Warnings(W)
		, Error(E)
	{
	}

	void Warn(const FString& Message)
	{
		if (Warned.insert(Message).second)
		{
			Warnings.Add(Message);
		}
	}
	FString MakeKey(const char* Prefix, const FString& Label, const FString& Identity);
	FFbxAssetData Data(EFbxAssetType Type, const FString& Key, const FString& Identity)
	{
		FFbxAssetData Out;
		Out.Type = Type;
		Out.Key = Key;
		Out.SourcePath = SourceKey;
		Out.SourceObject = Identity;
		Out.Options = Options;
		Out.ActualSourcePath = ActualSourceKey;
		Out.SourceHash = SourceHash;
		return Out;
	}
	void Gather(FbxNode* Node, bool bParentVisible, FbxNode* LODGroup = nullptr, int LOD = -1);
	void FindRigs();
	void PreparePoses();
	bool PoseMatrix(FbxPose* Pose, FbxNode* Node, FbxAMatrix& Out);
	FbxAMatrix Bind(FbxNode* Node);
	FbxAMatrix MeshBind(const FMeshItem& Item);
	bool BuildRig(FRig& Rig);
	bool MatchTargetSkeleton(FRig& Rig);
	bool CookAnimations(const FRig& Rig);
	bool CookAnimation(const FRig& Rig, FbxAnimStack* Stack, int StackIndex, int StackCount);
	FString Material(FbxSurfaceMaterial* SourceMaterial);
	FString Texture(FbxProperty Property);
	FString TextureFile(const fs::path& Path, const FString& Label, const FString& Identity);
	void ApplySidecarMaterial(FbxSurfaceMaterial* SourceMaterial, FFbxAssetData& Asset);
	FString OpacityTexture(FbxSurfaceMaterial* Material, const FString& DiffuseKey, const FString& OpacityKey, const FString& Identity, ESamplerState Sampler);
	FSurfaceUV SurfaceUV(FbxSurfaceMaterial* SourceMaterial, FbxMesh* Mesh, const FString& Context);
	bool CookMesh(const FOutputGroup& Group);
	bool AppendMesh(const FMeshItem& Item, FFbxAssetData& Asset, int LOD, std::map<uint32, std::vector<uint32>>& Sections);
	void LODThresholds(const FOutputGroup& Group, FFbxAssetData& Asset);
	bool Run();
};

FString FImport::MakeKey(const char* Prefix, const FString& Label, const FString& Identity)
{
	// 이름은 소유 FBX 기준이다. Reimport From File도 기존 파일을 덮어쓴다.
	const FString Base = FString(Prefix) + "_" + FbxPath::FileName(FbxPath::ToString(FbxPath::FromString(SourceKey).stem())) + (Label.empty() ? FString() : "_" + FbxPath::FileName(Label));
	FString Candidate = FbxPath::ToString(Destination / FbxPath::FromString(Base + ".fbxbin"));
	FString Key;
	if (!FbxPath::GetAssetKey(Candidate, Key, Error, false))
	{
		return {};
	}
	bool Conflict = false;
	if (auto It = FileOwners.find(Lower(Key)); It != FileOwners.end())
	{
		Conflict = It->second != Identity;
	}
	if (fs::exists(FbxPath::FromString(Candidate)))
	{
		FString PreviousSource, PreviousObject;
		// 같은 FBX의 파일은 덮어쓴다. 읽을 수 없거나 다른 FBX의 파일만 별도 안정 이름을 선택한다.
		if (!FFbxAssetBake::ReadSourceIdentity(Candidate, PreviousSource, PreviousObject))
		{
			Conflict = true;
		}
		else
		{
			Conflict |= Lower(PreviousSource) != Lower(SourceKey);
		}
	}
	if (Conflict)
	{
		std::ostringstream Hash;
		Hash << std::hex << FbxPath::StableHash(SourceKey + "/" + Identity);
		Candidate = FbxPath::ToString(Destination / FbxPath::FromString(Base + "_" + Hash.str() + ".fbxbin"));
		if (!FbxPath::GetAssetKey(Candidate, Key, Error, false))
		{
			return {};
		}
		if (fs::exists(FbxPath::FromString(Candidate)))
		{
			FString PreviousSource, PreviousObject;
			if (!FFbxAssetBake::ReadSourceIdentity(Candidate, PreviousSource, PreviousObject) ||
				Lower(PreviousSource) != Lower(SourceKey))
			{
				Error = "Output identity collision: " + Key;
				return {};
			}
		}
	}
	FileOwners[Lower(Key)] = Identity;
	return Key;
}

void FImport::Gather(FbxNode* Node, bool ParentVisible, FbxNode* Group, int LOD)
{
	const bool Visible = Node->Visibility.Get() > 0 && (!Node->VisibilityInheritance.Get() || ParentVisible);
	for (int A = 0; A < Node->GetNodeAttributeCount(); ++A)
	{
		auto* Attribute = Node->GetNodeAttributeByIndex(A);
		if (Attribute->GetAttributeType() != FbxNodeAttribute::eMesh || !Visible || CollisionName(Node->GetName()))
		{
			continue;
		}
		auto* Mesh = static_cast<FbxMesh*>(Attribute);
		FMeshItem Item;
		Item.Node = Node;
		Item.Mesh = Mesh;
		Item.LODGroupNode = Group;
		Item.LOD = LOD;
		Item.Label = Group ? Group->GetName() : Node->GetName();
		Item.UnitKey = Group ? NodePath(Group) : NodePath(Node) + "Mesh[" + std::to_string(A) + "]";
		if (!Group)
		{
			std::smatch Match;
			const FString Name = Node->GetName();
			if (std::regex_match(Name, Match, std::regex("(.+)_LOD([0-9]+)", std::regex::icase)))
			{
				Item.Label = Match[1].str();
				Item.UnitKey = NodePath(Node->GetParent()) + "NamedLOD/" + Item.Label;
				try
				{
					Item.LOD = std::stoi(Match[2].str());
				}
				catch (...)
				{
					Item.LOD = 4;
				}
			}
		}
		if (Item.LOD < 4)
		{
			Meshes.push_back(std::move(Item));
		}
		else
		{
			Warn("LOD4 and later geometry is omitted: " + Item.UnitKey);
		}
	}
	const bool NewGroup = !Group && HasAttribute(Node, FbxNodeAttribute::eLODGroup);
	for (int I = 0; I < Node->GetChildCount(); ++I)
	{
		Gather(Node->GetChild(I), Visible, NewGroup ? Node : Group, NewGroup ? I : LOD);
	}
}

void FImport::FindRigs()
{
	std::set<FbxNode*> Links;
	for (int N = 0; N < Scene->GetNodeCount(); ++N)
	{
		FbxNode* Node = Scene->GetNode(N);
		if (HasAttribute(Node, FbxNodeAttribute::eSkeleton))
		{
			Links.insert(Node);
		}
	}
	for (auto& Item : Meshes)
	{
		for (int D = 0; D < Item.Mesh->GetDeformerCount(FbxDeformer::eSkin); ++D)
		{
			auto* Skin = static_cast<FbxSkin*>(Item.Mesh->GetDeformer(D, FbxDeformer::eSkin));
			if (D)
			{
				Warn("Additional Skin deformers are ignored: " + NodePath(Item.Node));
			}
			if (Skin->GetSkinningType() != FbxSkin::eLinear && Skin->GetSkinningType() != FbxSkin::eRigid)
			{
				Warn("DQ/Blend skinning is imported as Linear: " + NodePath(Item.Node));
			}
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				if (!Cluster || !Cluster->GetLink() || IsCompensation(Cluster))
				{
					continue;
				}
				FbxNode* Link = Cluster->GetLink();
				Links.insert(Link);
				FbxAMatrix B, M;
				Cluster->GetTransformLinkMatrix(B);
				Cluster->GetTransformMatrix(M);
				if (auto It = ClusterBones.find(Link); It != ClusterBones.end())
				{
					if (!Near(It->second, B))
					{
						Warn("Conflicting bind matrices; selected bind pose retained: " + NodePath(Link));
					}
				}
				else
				{
					ClusterBones.emplace(Link, B);
				}
				if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end())
				{
					if (!Near(It->second, M))
					{
						Warn("Conflicting mesh bind matrices; first Cluster fallback retained: " + NodePath(Item.Node));
					}
				}
				else
				{
					ClusterMeshes.emplace(Item.Mesh, M);
				}
				if (D == 0)
				{
					FbxNode* Root = FindRoot(Scene, Link, bBlender);
					if (Item.Rig && Item.Rig != Root)
					{
						Error = "One mesh references incompatible rig roots: " + NodePath(Item.Node);
						return;
					}
					Item.Rig = Root;
				}
			}
		}
	}
	// Skin 없는 메시는 메시 노드를 본으로 삼아 스켈레탈로 만든다. 웨이트는 AppendMesh가 자기 노드에 1로 준다.
	// Skeletal만 켜면 모든 Skin 없는 메시가, Static도 켜면 Transform 키로 움직이는 메시만 대상이다.
	for (const auto& Item : Meshes)
	{
		if (!Item.Mesh->GetDeformerCount(FbxDeformer::eSkin) && (!Options.bImportStaticMeshes || MovesWithAnimation(Scene, Item.Node)))
		{
			Links.insert(Item.Node);
		}
	}
	for (FbxNode* Link : Links)
	{
		FbxNode* Root = FindRoot(Scene, Link, bBlender);
		if (!Root || Root == Scene->GetRootNode())
		{
			continue;
		}
		auto& Rig = Rigs[NodePath(Root)];
		Rig.Root = Root;
		for (FbxNode* N = Link; N && DescendsFrom(N, Root); N = N->GetParent())
		{
			Rig.Needed.insert(N);
		}
	}
	for (auto& Item : Meshes)
	{
		if (!Item.Rig)
		{
			for (auto& [Path, Rig] : Rigs)
			{
				if (DescendsFrom(Item.Node, Rig.Root) && (!Item.Rig || DescendsFrom(Rig.Root, Item.Rig)))
				{
					Item.Rig = Rig.Root;
				}
			}
		}
		if (Item.Rig)
		{
			auto& Rig = Rigs[NodePath(Item.Rig)];
			Rig.Root = Item.Rig;
			if (!Item.Mesh->GetDeformerCount(FbxDeformer::eSkin))
			{
				for (FbxNode* N = Item.Node; N && DescendsFrom(N, Rig.Root); N = N->GetParent())
				{
					Rig.Needed.insert(N);
				}
			}
		}
	}
}

void FImport::PreparePoses()
{
	if (!Scene->GetPoseCount())
	{
		Scene->GetFbxManager()->CreateMissingBindPoses(Scene);
	}
	for (int I = 0; I < Scene->GetPoseCount(); ++I)
	{
		FbxPose* Pose = Scene->GetPose(I);
		if (Pose && Pose->IsBindPose())
		{
			Poses.push_back(Pose);
		}
	}
	if (Poses.empty() && !Rigs.empty())
	{
		Warn("No usable BindPose; Cluster bind matrices and evaluated reference transforms are used");
	}
}

bool FImport::PoseMatrix(FbxPose* Pose, FbxNode* Node, FbxAMatrix& Out)
{
	const int Index = Pose->Find(Node);
	if (Index < 0)
	{
		return false;
	}
	// BindPose는 노드별로 검증한다.
	FbxArray<FbxNode*> Ancestors, Deformers, DeformerAncestors, Wrong;
	bool Valid = Pose->IsValidBindPoseVerbose(Node, Ancestors, Deformers, DeformerAncestors, Wrong, 1.e-4);
	if (!Valid)
	{
		for (int A = 0; A < Ancestors.GetCount(); ++A)
		{
			Pose->Add(Ancestors[A], Ancestors[A]->EvaluateGlobalTransform(FBXSDK_TIME_ZERO));
		}
		Valid = Pose->IsValidBindPose(Node);
		if (!Valid)
		{
			FbxNode* Parent = Node->GetParent();
			for (; Parent; Parent = Parent->GetParent())
			{
				if (auto* Attribute = Parent->GetNodeAttribute(); Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eNull)
				{
					break;
				}
			}
			Valid = Parent && Pose->IsValidBindPose(Parent);
		}
	}
	if (!Valid)
	{
		return false;
	}
	const FbxMatrix& Matrix = Pose->GetMatrix(Index);
	for (int R = 0; R < 4; ++R)
	{
		Out.SetRow(R, Matrix.GetRow(R));
	}
	if (Pose->IsLocalMatrix(Index))
	{
		Out = Bind(Node->GetParent()) * Out;
	}
	return true;
}

FbxAMatrix FImport::Bind(FbxNode* Node)
{
	if (!Node || Node == Scene->GetRootNode())
	{
		return FbxAMatrix();
	}
	if (auto It = BindCache.find(Node); It != BindCache.end())
	{
		return It->second;
	}
	FbxAMatrix Result;
	bool Found = false;
	for (FbxPose* Pose : Poses)
	{
		FbxAMatrix Candidate;
		if (!PoseMatrix(Pose, Node, Candidate))
		{
			continue;
		}
		if (Found)
		{
			if (!Near(Result, Candidate))
			{
				Warn("Multiple BindPoses differ; first valid pose retained: " + NodePath(Node));
			}
		}
		else
		{
			Result = Candidate;
			Found = true;
		}
	}
	if (auto It = ClusterBones.find(Node); It != ClusterBones.end())
	{
		if (Found && !Near(Result, It->second))
		{
			Warn("BindPose and Cluster differ; BindPose retained: " + NodePath(Node));
		}
		if (!Found)
		{
			Result = It->second;
			Found = true;
		}
	}
	if (!Found)
	{
		// BindPose에도 Cluster에도 없는 노드(웨이트 없는 _end 본 등)는 부모 바인드에 0초 로컬을 붙인다.
		// 0초 글로벌을 그대로 쓰면 장면의 0초 자세가 바인드 자세와 다를 때 부모에서 떨어져 나간다.
		FbxNode* Parent = Node->GetParent();
		const FbxAMatrix Global = Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO);
		Result = Parent && Parent != Scene->GetRootNode()
			? Bind(Parent) * Parent->EvaluateGlobalTransform(FBXSDK_TIME_ZERO).Inverse() * Global
			: Global;
	}
	BindCache.emplace(Node, Result);
	return Result;
}

FbxAMatrix FImport::MeshBind(const FMeshItem& Item)
{
	for (FbxPose* Pose : Poses)
	{
		FbxAMatrix Result;
		if (PoseMatrix(Pose, Item.Node, Result))
		{
			if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end() && !Near(Result, It->second))
			{
				Warn("Mesh BindPose and Cluster differ; BindPose retained: " + NodePath(Item.Node));
			}
			return Result;
		}
	}
	if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end())
	{
		return It->second;
	}
	return Bind(Item.Node);
}

bool FImport::BuildRig(FRig& Rig)
{
	Rig.ParentBind = Bind(Rig.Root->GetParent());
	Rig.FrameInverse = Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig.ParentBind.Inverse();
	const FbxAMatrix& FrameInverse = Rig.FrameInverse;
	if (!FiniteMatrix(EngineMatrix(Rig.ParentBind, UnitScale)))
	{
		Error = "Singular rig parent bind transform";
		return false;
	}
	std::function<bool(FbxNode*, int)> Visit = [&](FbxNode* Node, int Parent)
	{
		if (!Rig.Needed.contains(Node))
		{
			return true;
		}
		if (Rig.Bones.size() >= 65536)
		{
			Error = "More than 65536 bones in a rig";
			return false;
		}
		const uint16 Index = static_cast<uint16>(Rig.Bones.size());
		Rig.Bones.push_back(Node);
		Rig.Indices[Node] = Index;
		const FbxAMatrix Global = FrameInverse * Bind(Node);
		const FbxAMatrix Local = Parent < 0 ? Global : (FrameInverse * Bind(Rig.Bones[Parent])).Inverse() * Global;
		const FMatrix Converted = EngineMatrix(Local, UnitScale);
		if (!FiniteMatrix(Converted))
		{
			Error = "Invalid bind transform: " + NodePath(Node);
			return false;
		}
		const FBoneTransform Transform = EngineBoneTransform(Local, UnitScale);
		const FMatrix Rebuilt = Transform.ToMatrix();
		bool Difference = false;
		for (int R = 0; R < 4; ++R)
		{
			for (int C = 0; C < 4; ++C)
			{
				Difference |= std::abs(Rebuilt.M[R][C] - Converted.M[R][C]) > 1.e-3f * (1 + std::abs(Converted.M[R][C]));
			}
		}
		if (Difference)
		{
			Warn("Bone transform is approximated as TRS: " + NodePath(Node));
		}
		Rig.Reference.RefBoneInfo.Add({ Node->GetName(), Parent });
		Rig.Reference.RefBonePose.Add(Transform);
		for (int C = 0; C < Node->GetChildCount(); ++C)
		{
			if (!Visit(Node->GetChild(C), Index))
			{
				return false;
			}
		}
		return true;
	};
	if (!Visit(Rig.Root, -1) || !Rig.Reference.Validate(Error))
	{
		return false;
	}
	if (TargetSkeleton)
	{
		// 새 Skeleton은 만들지 않는다. 대상과 맞는 리그만 키를 받고, 맞지 않으면 애니메이션 대상에서 빠진다.
		Rig.Key = MatchTargetSkeleton(Rig) ? Options.TargetSkeletonKey : FString();
		return true;
	}
	Rig.Key = MakeKey("SKEL", Rig.Root->GetName(), "Skeleton:" + NodePath(Rig.Root));
	if (Rig.Key.empty())
	{
		return false;
	}
	auto Asset = Data(EFbxAssetType::Skeleton, Rig.Key, "Skeleton:" + NodePath(Rig.Root));
	Asset.RefSkeleton = Rig.Reference;
	Output.Add(std::move(Asset));
	return true;
}

// 리그의 본을 대상 Skeleton의 본에 이름으로 연결한다. 로컬 변환은 부모 기준이라 부모 이름까지 같아야 쓸 수 있다.
// 대상에 없는 본의 트랙은 버리고, FBX에 없는 대상 본은 트랙 없이 남겨 바인드 포즈를 쓰게 한다.
bool FImport::MatchTargetSkeleton(FRig& Rig)
{
	const FReferenceSkeleton& Target = TargetSkeleton->RefSkeleton;
	const int32 BoneCount = static_cast<int32>(Rig.Bones.size());
	Rig.TargetMask.assign(BoneCount, 0);
	int32 Matched = 0;
	float WorstError = 0.0f;
	FString WorstBone;
	for (int32 Bone = 0; Bone < BoneCount; ++Bone)
	{
		const FMeshBoneInfo& Info = Rig.Reference.RefBoneInfo[Bone];
		const int32 TargetBone = Target.FindBoneIndex(Info.Name);
		if (TargetBone < 0)
		{
			continue;
		}
		const int32 TargetParent = Target.RefBoneInfo[TargetBone].ParentIndex;
		const bool bSameParent = Info.ParentIndex < 0 ? TargetParent < 0
			: TargetParent >= 0 && Rig.Reference.RefBoneInfo[Info.ParentIndex].Name == Target.RefBoneInfo[TargetParent].Name;
		if (!bSameParent)
		{
			Warn("Bone hierarchy differs from the target skeleton at '" + Info.Name + "'; rig '" + FString(Rig.Root->GetName()) + "' is skipped");
			Rig.TargetMask.clear();
			return false;
		}
		Rig.TargetMask[Bone] = 1;
		++Matched;

		// 본 길이가 다르면 스케일이 다른 캐릭터다. 리타게팅은 하지 않으므로 알려 준다.
		const FVector& A = Rig.Reference.RefBonePose[Bone].Translation;
		const FVector& B = Target.RefBonePose[TargetBone].Translation;
		const float LengthA = std::sqrt(A.X * A.X + A.Y * A.Y + A.Z * A.Z);
		const float LengthB = std::sqrt(B.X * B.X + B.Y * B.Y + B.Z * B.Z);
		const float Longer = std::max(LengthA, LengthB);
		if (Longer > 1.e-3f && std::abs(LengthA - LengthB) / Longer > WorstError)
		{
			WorstError = std::abs(LengthA - LengthB) / Longer;
			WorstBone = Info.Name;
		}
	}
	if (!Matched)
	{
		Warn("No bone matches the target skeleton; rig '" + FString(Rig.Root->GetName()) + "' is skipped");
		Rig.TargetMask.clear();
		return false;
	}
	const int32 Required = static_cast<int32>(std::ceil(Options.MinTargetBoneMatch * std::min(BoneCount, Target.GetNum())));
	if (Matched < Required)
	{
		Warn("Only " + std::to_string(Matched) + " bones match the target skeleton (" + std::to_string(Required) + " required); rig '" +
			FString(Rig.Root->GetName()) + "' is skipped");
		Rig.TargetMask.clear();
		return false;
	}
	if (WorstError > 0.05f)
	{
		Warn("Bone length differs from the target skeleton by " + std::to_string(static_cast<int>(WorstError * 100.0f)) + "% at '" + WorstBone + "'; check Uniform Scale");
	}
	if (Matched < Target.GetNum())
	{
		Warn(std::to_string(Target.GetNum() - Matched) + " target bones have no animation track; they keep the bind pose");
	}
	HTR_LOG(Info, "[Anim] target skeleton {}: {}/{} FBX bones matched, {} target bones", Options.TargetSkeletonKey, Matched, BoneCount, Target.GetNum());
	return true;
}

// 씬의 시간 모드가 정한 프레임 레이트. 사용자 지정 모드면 그 값을, 알 수 없으면 30fps를 쓴다.
double SceneFrameRate(FbxScene* Scene)
{
	const FbxGlobalSettings& Settings = Scene->GetGlobalSettings();
	const FbxTime::EMode Mode = Settings.GetTimeMode();
	const double Rate = Mode == FbxTime::eCustom ? Settings.GetCustomFrameRate() : FbxTime::GetFrameRate(Mode);
	return std::isfinite(Rate) && Rate > 0.0 ? Rate : 30.0;
}

bool NearVector(const FVector& A, const FVector& B, float Tolerance)
{
	return std::abs(A.X - B.X) <= Tolerance && std::abs(A.Y - B.Y) <= Tolerance && std::abs(A.Z - B.Z) <= Tolerance;
}

bool NearQuat(const FQuat& A, const FQuat& B, float Tolerance)
{
	return std::abs(A.X - B.X) <= Tolerance && std::abs(A.Y - B.Y) <= Tolerance && std::abs(A.Z - B.Z) <= Tolerance &&
		std::abs(A.W - B.W) <= Tolerance;
}

// 모든 키가 첫 키와 같으면 1개로 줄인다(상수 트랙). 줄였으면 false, 움직이면 true를 돌려준다.
template<class T, class FNear>
bool ReduceConstantKeys(TArray<T>& Keys, FNear Near)
{
	for (int32 I = 1; I < Keys.Num(); ++I)
	{
		if (!Near(Keys[0], Keys[I]))
		{
			return true;
		}
	}
	if (Keys.Num() > 1)
	{
		Keys.SetNum(1);
	}
	return false;
}

bool FImport::CookAnimations(const FRig& Rig)
{
	// 샘플링은 현재 AnimStack을 바꿔 가며 평가한다. 끝나면 원래 스택으로 되돌린다.
	FbxAnimStack* Original = Scene->GetCurrentAnimationStack();
	bool bResult = true;
	for (int S = 0; S < Scene->GetSrcObjectCount<FbxAnimStack>() && bResult; ++S)
	{
		if (FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(S))
		{
			bResult = CookAnimation(Rig, Stack, S, Scene->GetSrcObjectCount<FbxAnimStack>());
		}
	}
	if (Original)
	{
		Scene->SetCurrentAnimationStack(Original);
	}
	return bResult;
}

bool FImport::CookAnimation(const FRig& Rig, FbxAnimStack* Stack, int StackIndex, int StackCount)
{
	const FString StackName = Stack->GetName();
	// 길이는 애니메이터가 찍은 마지막 키가 아니라 스택의 시간 범위로 정한다. 범위가 없으면 씬 타임라인 범위를 쓴다.
	FbxTimeSpan Span = Stack->GetLocalTimeSpan();
	if (Span.GetDuration().GetSecondDouble() <= 0.0)
	{
		Scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(Span);
	}
	const double Start = Span.GetStart().GetSecondDouble();
	const double Duration = std::max(0.0, Span.GetStop().GetSecondDouble() - Start);
	const double Rate = SceneFrameRate(Scene);
	if (!std::isfinite(Start) || !std::isfinite(Duration))
	{
		Warn("Animation has an invalid time range; skipped: " + StackName);
		return true;
	}

	// NumberOfKeys = 길이(초) × FrameRate + 1. 부동소수 오차(29.997프레임 등)는 반올림으로 프레임 격자에 맞춘다.
	const double FrameCount = std::round(Duration * Rate);
	const int32 BoneCount = static_cast<int32>(Rig.Bones.size());
	if (FrameCount + 1.0 > 100000.0 || (FrameCount + 1.0) * BoneCount > 50000000.0)
	{
		Error = "Animation is too long: " + StackName;
		return false;
	}

	FFbxAnimationData Animation;
	// 런타임은 정수 비율로 프레임 레이트를 다룬다. 정수 fps는 n/1, 그 밖(29.97 등)은 1/1000 단위로 맞춘다.
	if (std::abs(Rate - std::round(Rate)) < 1.e-4)
	{
		Animation.FrameRate = { static_cast<int32>(std::round(Rate)), 1 };
	}
	else
	{
		Animation.FrameRate = { static_cast<int32>(std::round(Rate * 1000.0)), 1000 };
	}
	Animation.Name = StackName;
	Animation.SourceTakeIndex = StackIndex;
	Animation.NumberOfKeys = static_cast<int32>(FrameCount) + 1;
	Animation.Tracks.SetNum(BoneCount);
	for (int32 Bone = 0; Bone < BoneCount; ++Bone)
	{
		auto& Track = Animation.Tracks[Bone];
		Track.BoneName = Rig.Reference.RefBoneInfo[Bone].Name;
		Track.InternalTrack.PosKeys.Reserve(Animation.NumberOfKeys);
		Track.InternalTrack.RotKeys.Reserve(Animation.NumberOfKeys);
		Track.InternalTrack.ScaleKeys.Reserve(Animation.NumberOfKeys);
	}

	// 애니메이터의 키와 상관없이 프레임마다 SDK로 곡선을 평가해 키를 만든다(베이크).
	// 로컬은 BuildRig의 바인드 포즈와 같은 식이다: (Frame × 부모 글로벌)⁻¹ × (Frame × 글로벌)
	Scene->SetCurrentAnimationStack(Stack);
	std::vector<FbxAMatrix> Globals(BoneCount);
	for (int32 Key = 0; Key < Animation.NumberOfKeys; ++Key)
	{
		FbxTime Time;
		Time.SetSecondDouble(Start + std::min(Key / Rate, Duration));
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			// 본은 부모가 먼저 오는 순서라 부모 글로벌은 이미 구해져 있다.
			Globals[Bone] = Rig.FrameInverse * Rig.Bones[Bone]->EvaluateGlobalTransform(Time);
			const int32 Parent = Rig.Reference.RefBoneInfo[Bone].ParentIndex;
			const FbxAMatrix Local = Parent < 0 ? Globals[Bone] : Globals[Parent].Inverse() * Globals[Bone];
			FBoneTransform Transform = EngineBoneTransform(Local, UnitScale);

			auto& Keys = Animation.Tracks[Bone].InternalTrack;
			// q와 -q는 같은 회전이다. 앞 키와 같은 쪽 부호로 맞춰 두면 상수 판정과 보간이 안정된다.
			if (!Keys.RotKeys.IsEmpty())
			{
				const FQuat& Previous = Keys.RotKeys[Keys.RotKeys.Num() - 1];
				FQuat& Q = Transform.Rotation;
				if (Previous.X * Q.X + Previous.Y * Q.Y + Previous.Z * Q.Z + Previous.W * Q.W < 0.0f)
				{
					Q = FQuat(-Q.X, -Q.Y, -Q.Z, -Q.W);
				}
			}
			Keys.PosKeys.Add(Transform.Translation);
			Keys.RotKeys.Add(Transform.Rotation);
			Keys.ScaleKeys.Add(Transform.Scale);
		}
	}

	// Target Skeleton 모드: 대상에 없는 본의 트랙은 버린다. 위 계산은 부모 글로벌이 필요해 모든 본으로 했다.
	if (!Rig.TargetMask.empty())
	{
		TArray<FFbxBoneTrack> Kept;
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			if (Rig.TargetMask[Bone])
			{
				Kept.Add(Animation.Tracks[Bone]);
			}
		}
		Animation.Tracks = std::move(Kept);
	}

	// 변하지 않는 성분은 키 1개로 줄인다. 위치는 엔진 단위, 회전은 쿼터니언 성분, 크기는 배율 기준 허용 오차다.
	int32 AnimatedBones = 0;
	for (auto& Track : Animation.Tracks)
	{
		auto& Keys = Track.InternalTrack;
		bool bAnimated = ReduceConstantKeys(Keys.PosKeys, [](const FVector& A, const FVector& B) { return NearVector(A, B, 1.e-4f); });
		bAnimated |= ReduceConstantKeys(Keys.RotKeys, [](const FQuat& A, const FQuat& B) { return NearQuat(A, B, 1.e-5f); });
		bAnimated |= ReduceConstantKeys(Keys.ScaleKeys, [](const FVector& A, const FVector& B) { return NearVector(A, B, 1.e-5f); });
		AnimatedBones += bAnimated ? 1 : 0;
	}
	const FString Identity = "Anim:" + NodePath(Rig.Root) + "/" + StackName;
	if (!AnimatedBones)
	{
		// 메시 FBX에 흔한 빈 기본 테이크("Take 001" 등)는 에셋을 만들지 않는다.
		HTR_LOG(Info, "[Anim] {}: no bone moves; skipped", Identity);
		return true;
	}

	// 스택이 하나면 AS_<FBX 이름>, 여럿이면 AS_<FBX 이름>_<스택 이름>이다. JSON 애셋이 이 키를 참조하므로 규칙을 바꾸지 않는다.
	const FString Key = MakeKey("AS", StackCount > 1 ? StackName : FString(), Identity);
	if (Key.empty())
	{
		return false;
	}
	HTR_LOG(Info, "[Anim] {}: source {:.3f}s ~ {:.3f}s, {:.2f} fps, {} keys, {:.3f}s, {}/{} bones animated",
		Key, Start, Start + Duration, Rate, Animation.NumberOfKeys, (Animation.NumberOfKeys - 1) / Rate, AnimatedBones, Animation.Tracks.Num());
	auto Asset = Data(EFbxAssetType::AnimSequence, Key, Identity);
	Asset.SkeletonKey = Rig.Key;
	Asset.Animation = std::move(Animation);
	Output.Add(std::move(Asset));
	return true;
}

FSurfaceUV FImport::SurfaceUV(FbxSurfaceMaterial* Material, FbxMesh* Mesh, const FString& Context)
{
	FSurfaceUV Out;
	FbxStringList Names;
	Mesh->GetUVSetNames(Names);
	const FString First = Names.GetCount() ? Names[0].Buffer() : "";
	FbxFileTexture* Inputs[]{ FileTexture(Material, FbxSurfaceMaterial::sDiffuse), FileTexture(Material, FbxSurfaceMaterial::sNormalMap),
		SkippedOpacityTextures.contains(Material) ? nullptr : FileTexture(Material, FbxSurfaceMaterial::sTransparentColor) };
	FString Sets[3];
	bool Missing = false;
	for (int I = 0; I < 3; ++I)
	{
		if (Inputs[I])
		{
			const FString Requested = Inputs[I]->UVSet.Get().Buffer();
			for (int N = 0; N < Names.GetCount(); ++N)
			{
				if (Requested == Names[N].Buffer())
				{
					Sets[I] = Requested;
					break;
				}
			}
			if (Sets[I].empty() && (Requested.empty() || Requested == "default"))
			{
				Sets[I] = First;
			}
			if (Sets[I].empty())
			{
				Missing = true;
				Warn("UV set not found: " + Context + ", texture=" + Inputs[I]->GetName() + ", requested=" + Requested);
			}
		}
	}
	Out.Set = First;
	for (const auto& Set : Sets)
	{
		if (!Set.empty())
		{
			Out.Set = Set;
			break;
		}
	}
	bool DifferentSets = false;
	for (int I = 0; I < 3; ++I)
	{
		DifferentSets |= !Missing && Inputs[I] && Sets[I] != Out.Set;
	}
	if (DifferentSets)
	{
		Warn("Different texture UV sets: " + Context + ", diffuse=" + Sets[0] + ", normal=" + Sets[1] + ", opacity=" + Sets[2] + "; selected=" + Out.Set + ", UV transforms ignored");
	}
	else if (Missing)
	{
		Warn("UV fallback: " + Context + "; selected=" + Out.Set + ", UV transforms ignored");
	}
	else
	{
		FUVTransform Selected;
		bool HasSelected = false, Invalid = false, Different = false;
		for (auto* Input : Inputs)
		{
			if (Input)
			{
				const FUVTransform Transform = UVTransform(Input);
				Invalid |= !Transform.bValid;
				if (HasSelected)
				{
					Different |= !Near(Selected, Transform);
				}
				else
				{
					Selected = Transform;
					HasSelected = true;
				}
			}
		}
		if (Invalid)
		{
			Warn("Unsupported or invalid texture UV mapping: " + Context + "; UV transforms ignored");
		}
		else if (Different)
		{
			Warn("Different texture UV transforms: " + Context + "; selected=" + Out.Set + ", UV transforms ignored");
		}
		else
		{
			Out.Transform = Selected;
		}
	}
	// UV 세트가 속한 레이어의 탄젠트를 사용한다. 다른 세트의 기저를 재사용하지 않는다.
	for (int L = 0; L < Mesh->GetLayerCount(); ++L)
	{
		auto* Layer = Mesh->GetLayer(L);
		const auto UVSets = Layer->GetUVSets();
		for (int U = 0; U < UVSets.GetCount(); ++U)
		{
			if (UVSets[U] && Out.Set == UVSets[U]->GetName())
			{
				Out.Tangents = Layer->GetTangents();
				Out.Binormals = Layer->GetBinormals();
				return Out;
			}
		}
	}
	return Out;
}

FString FImport::Texture(FbxProperty Property)
{
	auto* T = PropertyTexture(Property);
	if (!T)
	{
		return {};
	}
	if (Property.GetSrcObjectCount<FbxLayeredTexture>() && !Property.GetSrcObjectCount<FbxFileTexture>())
	{
		Warn("Layered texture uses its first layer only: " + FString(Property.GetNameAsCStr()));
	}
	if (Property.GetSrcObjectCount<FbxFileTexture>() > 1)
	{
		Warn("Only the first texture connection is used: " + FString(Property.GetNameAsCStr()));
	}
	std::vector<fs::path> Candidates;
	if (T->GetFileName()[0])
	{
		Candidates.push_back(FbxPath::FromString(T->GetFileName()));
	}
	if (T->GetRelativeFileName()[0])
	{
		Candidates.push_back(Source.parent_path() / FbxPath::FromString(T->GetRelativeFileName()));
	}
	if (T->GetFileName()[0])
	{
		// 절대 경로는 만든 사람의 PC 기준이라 파일 이름으로 FBX 옆, 자동 언팩 폴더, Textures를 찾는다.
		const fs::path Name = FbxPath::FromString(T->GetFileName()).filename();
		Candidates.push_back(Source.parent_path() / Name);
		Candidates.push_back(Source.parent_path() / (Source.stem().wstring() + L".fbm") / Name);
		Candidates.push_back(Source.parent_path() / "Textures" / Name);
	}
	FString SourceTexture;
	std::error_code FileError;
	for (auto Candidate : Candidates)
	{
		if (!Candidate.is_absolute())
		{
			Candidate = Source.parent_path() / Candidate;
		}
		if (fs::is_regular_file(Candidate, FileError))
		{
			SourceTexture = FbxPath::ToString(Candidate);
			break;
		}
	}
	// 텍스처 하나가 없다고 메시까지 잃지 않는다. 머티리얼은 FBX의 상수 색으로 남는다.
	if (SourceTexture.empty())
	{
		Warn("Texture file not found: " + FString(T->GetFileName()));
		return {};
	}
	// 임시 내장 경로 대신 소스 FBX 안의 논리 이름으로 출력 이름을 고정한다.
	int TextureIndex = 0;
	for (; TextureIndex < Scene->GetTextureCount() && Scene->GetTexture(TextureIndex) != T; ++TextureIndex)
	{
	}
	const FString Identity = "Texture:" + FString(T->GetName()) + "[" + std::to_string(TextureIndex) + "]/" + FbxPath::ToString(FbxPath::FromString(T->GetFileName()).filename());
	return TextureFile(FbxPath::FromString(SourceTexture), T->GetName(), Identity);
}

FString FImport::TextureFile(const fs::path& Path, const FString& Label, const FString& Identity)
{
	// 입력 텍스처도 원본과 같이 외부에서 읽을 수 있다. 결과에는 픽셀/압축 블록만 남긴다.
	std::error_code Ec;
	const fs::path Canonical = fs::canonical(Path, Ec);
	if (Ec || !fs::is_regular_file(Canonical))
	{
		Warn("Texture file not found: " + FbxPath::ToString(Path));
		return {};
	}
	const FString SourceTexture = FbxPath::ToString(Canonical);
	if (auto It = Textures.find(Lower(SourceTexture)); It != Textures.end()) return It->second;
	FImageData Image = ImageLoader::LoadAuto(SourceTexture);
	if (!Image.IsValid())
	{
		Warn("Texture format is not supported by the existing image loader: " + SourceTexture);
		return {};
	}
	const FString Key = MakeKey("T", Label, Identity);
	if (Key.empty())
	{
		return {};
	}
	auto Asset = Data(EFbxAssetType::Texture2D, Key, Identity);
	Asset.Texture = std::move(Image);
	Output.Add(std::move(Asset));
	Textures[Lower(SourceTexture)] = Key;
	return Key;
}

void FImport::ApplySidecarMaterial(FbxSurfaceMaterial* SourceMaterial, FFbxAssetData& Asset)
{
	const FString MaterialName = SourceMaterial->GetName();
	FString DirectDiffuse, DirectNormal;
	if (MaterialName.starts_with("MI_"))
	{
		const FString TextureStem = "T_" + MaterialName.substr(3);
		const auto ReadDirect = [&](const FString& Suffix, const FString& Usage) -> FString
		{
			for (const char* Extension : { ".png", ".PNG" })
			{
				const fs::path Path = Source.parent_path() / FbxPath::FromString(TextureStem + Suffix + Extension);
				if (fs::is_regular_file(Path))
					return TextureFile(Path, MaterialName + "_" + Usage,
						"TextureFile:" + MaterialName + "/" + Usage + "/" + FbxPath::ToString(Path.filename()));
			}
			return {};
		};
		DirectDiffuse = ReadDirect("_D", "BaseColor");
		DirectNormal = ReadDirect("_N", "Normal");
		if (!DirectDiffuse.empty())
		{
			Asset.Material.DiffuseTexture = DirectDiffuse;
			Asset.Material.DiffuseOrFactor.X = Asset.Material.DiffuseOrFactor.Y = Asset.Material.DiffuseOrFactor.Z = 1.0f;
		}
		if (!DirectNormal.empty()) Asset.Material.NormalTexture = DirectNormal;
	}
	if (!DirectDiffuse.empty() && !DirectNormal.empty()) return;
	const fs::path GltfPath = Source.parent_path() / FbxPath::FromString(MaterialName + ".gltf");
	if (!fs::is_regular_file(GltfPath)) return;
	std::ifstream In(GltfPath);
	const json Gltf = json::parse(In, nullptr, false);
	if (!Gltf.is_object() || !Gltf.contains("materials") || !Gltf["materials"].is_array() || Gltf["materials"].empty() ||
		!Gltf.contains("textures") || !Gltf["textures"].is_array() || !Gltf.contains("images") || !Gltf["images"].is_array())
	{
		Warn("Invalid glTF material: " + FbxPath::ToString(GltfPath));
		return;
	}
	const json* Material = nullptr;
	for (const json& Candidate : Gltf["materials"])
	{
		if (!Candidate.is_object() || !Candidate.contains("name") || !Candidate["name"].is_string()) continue;
		const FString Name = Candidate["name"].get<FString>();
		if (Name == MaterialName || Name.starts_with(MaterialName + "_")) { Material = &Candidate; break; }
	}
	if (!Material)
	{
		Warn("glTF material name does not match FBX slot: " + MaterialName);
		return;
	}
	const auto ReadTexture = [&](const json& Reference, const FString& Usage) -> FString
	{
		if (!Reference.is_object() || !Reference.contains("index") || !Reference["index"].is_number_integer()) return {};
		if ((Reference.contains("texCoord") && (!Reference["texCoord"].is_number_integer() || Reference["texCoord"].get<int>() != 0)) ||
			(Reference.contains("extensions") && Reference["extensions"].is_object() && Reference["extensions"].contains("KHR_texture_transform")))
		{
			Warn("glTF texture needs an unsupported UV set or transform; skipped: " + MaterialName + "/" + Usage);
			return {};
		}
		const int TextureIndex = Reference["index"].get<int>();
		if (TextureIndex < 0 || static_cast<size_t>(TextureIndex) >= Gltf["textures"].size()) return {};
		const json& Texture = Gltf["textures"][TextureIndex];
		if (!Texture.is_object() || !Texture.contains("source") || !Texture["source"].is_number_integer()) return {};
		const int ImageIndex = Texture["source"].get<int>();
		if (ImageIndex < 0 || static_cast<size_t>(ImageIndex) >= Gltf["images"].size()) return {};
		const json& Image = Gltf["images"][ImageIndex];
		if (!Image.is_object() || !Image.contains("uri") || !Image["uri"].is_string()) return {};
		const fs::path Uri = FbxPath::FromString(Image["uri"].get<FString>());
		if (Uri.is_absolute() || Uri.has_root_name()) return {};
		for (const auto& Part : Uri) if (Part == "..") return {};
		return TextureFile(GltfPath.parent_path() / Uri, MaterialName + "_" + Usage,
			"GLTFTexture:" + MaterialName + "/" + Usage + "/" + FbxPath::ToString(Uri));
	};
	const json Pbr = Material->value("pbrMetallicRoughness", json::object());
	const FString Diffuse = DirectDiffuse.empty()
		? ReadTexture(Pbr.is_object() ? Pbr.value("baseColorTexture", json::object()) : json::object(), "BaseColor") : DirectDiffuse;
	if (!Diffuse.empty())
	{
		Asset.Material.DiffuseTexture = Diffuse;
		Asset.Material.DiffuseOrFactor.X = Asset.Material.DiffuseOrFactor.Y = Asset.Material.DiffuseOrFactor.Z = 1.0f;
	}
	const FString Normal = DirectNormal.empty() ? ReadTexture(Material->value("normalTexture", json::object()), "Normal") : DirectNormal;
	if (!Normal.empty()) Asset.Material.NormalTexture = Normal;
}

FString FImport::OpacityTexture(FbxSurfaceMaterial* Material, const FString& DiffuseKey, const FString& OpacityKey,
	const FString& Identity, ESamplerState Sampler)
{
	const FImageData* Diffuse = nullptr;
	const FImageData* Opacity = nullptr;
	for (const auto& Asset : Output)
	{
		if (Asset.Type == EFbxAssetType::Texture2D)
		{
			if (Asset.Key == DiffuseKey)
			{
				Diffuse = &Asset.Texture;
			}
			if (Asset.Key == OpacityKey)
			{
				Opacity = &Asset.Texture;
			}
		}
	}
	if (!Opacity)
	{
		Error = "Missing opacity image: " + FString(Material->GetName());
		return {};
	}
	auto Supported = [](const FImageData& Image)
	{
		switch (Image.Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return Image.BytesPerPixel == 4;
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_A8_UNORM:
			return Image.BytesPerPixel == 1;
		default:
			return false;
		}
	};
	if (!Supported(*Opacity) || (Diffuse && !Supported(*Diffuse)))
	{
		Warn("Opacity baking skipped: " + FString(Material->GetName()) +
			"; unsupported image format (diffuse=" + (Diffuse ? std::to_string(Diffuse->Format) : "none") +
			", opacity=" + std::to_string(Opacity->Format) + "); original Diffuse and scalar opacity used");
		return {};
	}
	auto Pixel = [](const FImageData& Image, uint32 X, uint32 Y) -> std::array<uint8, 4>
	{
		const uint8* P = Image.Pixels.GetData() + static_cast<size_t>(Y) * Image.GetRowPitch() + X * Image.BytesPerPixel;
		switch (Image.Format)
		{
		case DXGI_FORMAT_R8_UNORM:
			return { P[0], P[0], P[0], 255 };
		case DXGI_FORMAT_A8_UNORM:
			return { 255, 255, 255, P[0] };
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return { P[2], P[1], P[0], P[3] };
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return { P[2], P[1], P[0], 255 };
		default:
			return { P[0], P[1], P[2], P[3] };
		}
	};
	const auto AlphaSource = FileTexture(Material, FbxSurfaceMaterial::sTransparentColor)->GetAlphaSource();
	bool UseAlpha = AlphaSource == FbxTexture::eBlack;
	if (AlphaSource == FbxTexture::eNone)
	{
		for (uint32 Y = 0; Y < Opacity->Height && !UseAlpha; ++Y)
		{
			for (uint32 X = 0; X < Opacity->Width && !UseAlpha; ++X)
			{
				UseAlpha = Pixel(*Opacity, X, Y)[3] != 255;
			}
		}
	}
	auto Address = [Sampler](int Value, uint32 Size)
	{
		const int Count = static_cast<int>(Size);
		return static_cast<uint32>(Sampler == ESamplerState::LinearWrap ? ((Value % Count) + Count) % Count : std::clamp(Value, 0, Count - 1));
	};
	auto OpacityAt = [&](int X, int Y)
	{
		const auto P = Pixel(*Opacity, Address(X, Opacity->Width), Address(Y, Opacity->Height));
		// 알파/불투명도 마스크는 수치 데이터다. RGB 밝기를 사용할 때도 sRGB 디코딩하지 않는다.
		return UseAlpha ? static_cast<double>(P[3]) : .2126 * P[0] + .7152 * P[1] + .0722 * P[2];
	};
	FImageData Image;
	Image.Width = Diffuse ? Diffuse->Width : Opacity->Width;
	Image.Height = Diffuse ? Diffuse->Height : Opacity->Height;
	Image.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Image.BytesPerPixel = 4;
	Image.Pixels.SetNum(static_cast<int32>(Image.Width * Image.Height * 4));
	for (uint32 Y = 0; Y < Image.Height; ++Y)
	{
		for (uint32 X = 0; X < Image.Width; ++X)
		{
			const auto Color = Diffuse ? Pixel(*Diffuse, X, Y) : std::array<uint8, 4>{ 255, 255, 255, 255 };
			uint8* P = Image.Pixels.GetData() + (static_cast<size_t>(Y) * Image.Width + X) * 4;
			std::copy_n(Color.begin(), 3, P);
			const double SX = (X + .5) * Opacity->Width / Image.Width - .5, SY = (Y + .5) * Opacity->Height / Image.Height - .5;
			const int IX = static_cast<int>(std::floor(SX)), IY = static_cast<int>(std::floor(SY));
			const double FX = SX - IX, FY = SY - IY;
			const double Top = OpacityAt(IX, IY) * (1 - FX) + OpacityAt(IX + 1, IY) * FX;
			const double Bottom = OpacityAt(IX, IY + 1) * (1 - FX) + OpacityAt(IX + 1, IY + 1) * FX;
			P[3] = static_cast<uint8>(std::clamp(std::lround(Top * (1 - FY) + Bottom * FY), 0l, 255l));
		}
	}
	const FString SourceObject = "OpacityTexture:" + Identity;
	const FString Key = MakeKey("T", FString(Material->GetName()) + "_Opacity", SourceObject);
	if (Key.empty())
	{
		return {};
	}
	auto Asset = Data(EFbxAssetType::Texture2D, Key, SourceObject);
	Asset.Texture = std::move(Image);
	Output.Add(std::move(Asset));
	return Key;
}

FString FImport::Material(FbxSurfaceMaterial* M)
{
	if (!M)
	{
		return "DefaultMaterial";
	}
	if (auto It = Materials.find(M); It != Materials.end())
	{
		return It->second;
	}
	int SourceIndex = 0;
	for (; SourceIndex < Scene->GetMaterialCount() && Scene->GetMaterial(SourceIndex) != M; ++SourceIndex)
	{
	}
	const FString Identity = "Material:" + FString(M->GetName()) + "[" + std::to_string(SourceIndex) + "]";
	const FString Key = MakeKey("M", M->GetName(), Identity);
	if (Key.empty())
	{
		return {};
	}
	auto Asset = Data(EFbxAssetType::Material, Key, Identity);
	float DiffuseFactor = 1.f;
	if (auto* Lambert = FbxCast<FbxSurfaceLambert>(M))
	{
		const FbxDouble3 Color = Lambert->Diffuse.Get();
		const double Factor = Lambert->DiffuseFactor.Get();
		DiffuseFactor = static_cast<float>(Factor);
		// 투명도는 TransparentColor × TransparencyFactor다(FBX SDK 정의). Maya/Mixamo는 색을 검정, 계수를 1로 써서 불투명을 나타내므로
		// 계수만 보면 완전 투명으로 읽힌다. Blender는 색 (1,1,1)에 계수 1-알파를 쓰므로 이 식으로도 알파가 같다.
		const FbxDouble3 TransparentColor = Lambert->TransparentColor.Get();
		const double Transparency = std::clamp((TransparentColor[0] + TransparentColor[1] + TransparentColor[2]) / 3. * Lambert->TransparencyFactor.Get(), 0., 1.);
		// Diffuse를 선형 색상으로 해석하는 임포트 기본값이다. 강도 계수도 선형 공간에서 곱한다.
		Asset.Material.DiffuseOrFactor = FVector4(static_cast<float>(Color[0] * Factor), static_cast<float>(Color[1] * Factor),
			static_cast<float>(Color[2] * Factor), static_cast<float>(1. - Transparency));
		Asset.Material.ShadingModel = ELightingModel::Lambert;
		Asset.Material.SpecularFactor = 0.f;
		const FbxDouble3 Emissive = Lambert->Emissive.Get();
		Asset.Material.EmissiveColor = FVector(static_cast<float>(Emissive[0]), static_cast<float>(Emissive[1]), static_cast<float>(Emissive[2]));
		Asset.Material.EmissiveFactor = static_cast<float>(Lambert->EmissiveFactor.Get());
	}
	else
	{
		Warn("Unsupported material model; engine default used: " + FString(M->GetName()));
	}
	if (auto* Phong = FbxCast<FbxSurfacePhong>(M))
	{
		Asset.Material.ShadingModel = ELightingModel::Phong;
		const FbxDouble3 Specular = Phong->Specular.Get();
		Asset.Material.SpecularColor = FVector(static_cast<float>(Specular[0]), static_cast<float>(Specular[1]), static_cast<float>(Specular[2]));
		Asset.Material.SpecularFactor = static_cast<float>(Phong->SpecularFactor.Get());
		// Phong의 반사 벡터 지수를 Blinn-Phong의 하프 벡터 지수로 근사한다.
		Asset.Material.Shininess = static_cast<float>(4. * std::max(0., Phong->Shininess.Get()));
	}
	for (const char* PropertyName : { FbxSurfaceMaterial::sSpecular, FbxSurfaceMaterial::sSpecularFactor,
			 FbxSurfaceMaterial::sEmissive, FbxSurfaceMaterial::sEmissiveFactor })
	{
		const FbxProperty Property = M->FindProperty(PropertyName);
		if (Property.IsValid() && Property.GetSrcObjectCount<FbxTexture>() > 0)
		{
			Warn("Unsupported material texture: " + FString(M->GetName()) + ", property=" + PropertyName + "; constant color/factor used");
		}
	}
	auto* Diffuse = FileTexture(M, FbxSurfaceMaterial::sDiffuse);
	auto* Normal = FileTexture(M, FbxSurfaceMaterial::sNormalMap);
	auto* Opacity = FileTexture(M, FbxSurfaceMaterial::sTransparentColor);
	auto* Sampler = Diffuse ? Diffuse : (Normal ? Normal : Opacity);
	const FString SamplerName = Diffuse ? "Diffuse" : (Normal ? "Normal" : "Opacity");
	for (auto* Input : { Diffuse, Normal, Opacity })
	{
		if (Input)
		{
			if (Input != Sampler && (Sampler->GetWrapModeU() != Input->GetWrapModeU() || Sampler->GetWrapModeV() != Input->GetWrapModeV()))
			{
				Warn("Different texture Wrap modes: " + FString(M->GetName()) + "; " + SamplerName + " Wrap selected");
			}
			if (Input->GetWrapModeU() != Input->GetWrapModeV())
			{
				Warn("Unsupported mixed U/V Wrap modes: " + FString(M->GetName()) + ", texture=" + Input->GetName() +
					(Input == Sampler ? "; LinearClamp selected" : "; " + SamplerName + " Wrap selected"));
			}
		}
	}
	if (Sampler && Sampler->GetWrapModeU() == Sampler->GetWrapModeV() && Sampler->GetWrapModeU() == FbxTexture::eRepeat)
	{
		Asset.Material.SamplerState = ESamplerState::LinearWrap;
	}
	Asset.Material.DiffuseTexture = Texture(MaterialInput(M, FbxSurfaceMaterial::sDiffuse));
	if (!Error.empty())
	{
		return {};
	}
	if (!Asset.Material.DiffuseTexture.empty())
	{
		// 색상 맵이 있으면 DiffuseFactor를 RGB 공통 배율로 사용한다. W는 불투명도다.
		Asset.Material.DiffuseOrFactor.X = Asset.Material.DiffuseOrFactor.Y = Asset.Material.DiffuseOrFactor.Z = DiffuseFactor;
	}
	Asset.Material.NormalTexture = Texture(MaterialInput(M, FbxSurfaceMaterial::sNormalMap));
	if (!Error.empty())
	{
		return {};
	}
	Asset.Material.BlendState = Asset.Material.DiffuseOrFactor.W < 1.f ? EBlendState::AlphaBlend : EBlendState::Opaque;
	ApplySidecarMaterial(M, Asset);
	const FString OpacityKey = Texture(M->FindProperty(FbxSurfaceMaterial::sTransparentColor));
	if (!Error.empty())
	{
		return {};
	}
	if (!OpacityKey.empty())
	{
		const FString BakedTexture = OpacityTexture(M, Asset.Material.DiffuseTexture, OpacityKey, Identity, Asset.Material.SamplerState);
		if (!Error.empty())
		{
			return {};
		}
		if (!BakedTexture.empty())
		{
			Asset.Material.DiffuseTexture = BakedTexture;
			// 연결된 맵이 불투명도 입력을 제공한다. 상수 계수와 원본 Diffuse 알파를 중복 적용하지 않는다.
			Asset.Material.DiffuseOrFactor.W = 1.f;
			Asset.Material.BlendState = EBlendState::AlphaBlend;
		}
		else
		{
			SkippedOpacityTextures.insert(M);
		}
	}
	{
		// 원본 값과 변환 결과를 함께 남겨, 재질이 어긋났을 때 어느 단계에서 바뀌었는지 볼 수 있게 한다.
		const auto& Out = Asset.Material;
		const FbxProperty RawTransparent = M->FindProperty(FbxSurfaceMaterial::sTransparentColor);
		const FbxProperty RawFactor = M->FindProperty(FbxSurfaceMaterial::sTransparencyFactor);
		const FbxProperty RawOpacity = M->FindProperty("Opacity");
		const FbxDouble3 RawColor = RawTransparent.IsValid() ? RawTransparent.Get<FbxDouble3>() : FbxDouble3();
		HTR_LOG(Info, "[Material] {}: raw TransparentColor=({:.3f},{:.3f},{:.3f}) TransparencyFactor={:.3f} Opacity={:.3f}",
			M->GetName(), RawColor[0], RawColor[1], RawColor[2], RawFactor.IsValid() ? RawFactor.Get<double>() : -1.0, RawOpacity.IsValid() ? RawOpacity.Get<double>() : -1.0);
		HTR_LOG(Info, "[Material] {}: model={} diffuse=({:.3f},{:.3f},{:.3f}) alpha={:.3f} specular=({:.3f},{:.3f},{:.3f})x{:.3f} shininess={:.3f} emissive=({:.3f},{:.3f},{:.3f})x{:.3f} blend={} diffuseTex='{}' normalTex='{}'",
			M->GetName(), Out.ShadingModel == ELightingModel::Phong ? "Phong" : "Lambert", Out.DiffuseOrFactor.X, Out.DiffuseOrFactor.Y, Out.DiffuseOrFactor.Z, Out.DiffuseOrFactor.W,
			Out.SpecularColor.X, Out.SpecularColor.Y, Out.SpecularColor.Z, Out.SpecularFactor, Out.Shininess,
			Out.EmissiveColor.X, Out.EmissiveColor.Y, Out.EmissiveColor.Z, Out.EmissiveFactor,
			Out.BlendState == EBlendState::Opaque ? "Opaque" : "AlphaBlend", Out.DiffuseTexture, Out.NormalTexture);
	}
	Output.Add(std::move(Asset));
	Materials[M] = Key;
	return Key;
}

bool FImport::AppendMesh(const FMeshItem& Item, FFbxAssetData& Asset, int LODIndex, std::map<uint32, std::vector<uint32>>& Sections)
{
	auto& LOD = Asset.LODs[LODIndex];
	auto& Mesh = LOD.MeshData;
	FRig* Rig = Item.Rig ? &Rigs.at(NodePath(Item.Rig)) : nullptr;
	FbxAMatrix Full;
	if (Rig)
	{
		// Cluster.Transform은 이미 글로벌이다. Bake off는 루트 부모만 제거한다.
		Full = (Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig->ParentBind.Inverse()) * MeshBind(Item) * Geometry(Item.Node);
	}
	else if (Options.bBakeStaticMeshes)
	{
		Full = Item.Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO) * Geometry(Item.Node);
	}
	// Static Bake off는 자기 노드 변환도 적용하지 않는다. Combine이 배치를 대신 복원하지 않는다.
	const FMatrix Transform = EngineMatrix(Full, UnitScale, false);
	if (!FiniteMatrix(Transform))
	{
		Error = "Invalid mesh transform: " + NodePath(Item.Node);
		return false;
	}
	const FMatrix NormalMatrix = Transform.Inverse().GetTransposed();
	const bool Reverse = Transform.Determinant() < 0;
	std::vector<uint32> MaterialMap;
	std::vector<FSurfaceUV> Surfaces;
	std::vector<int> SurfaceGroups;
	const int SlotCount = std::max(1, Item.Node->GetMaterialCount());
	for (int I = 0; I < SlotCount; ++I)
	{
		FbxSurfaceMaterial* SourceMaterial = I < Item.Node->GetMaterialCount() ? Item.Node->GetMaterial(I) : nullptr;
		const FString Key = Material(SourceMaterial);
		if (!Error.empty())
		{
			return false;
		}
		const FSurfaceUV UV = SurfaceUV(SourceMaterial, Item.Mesh, NodePath(Item.Node) + (SourceMaterial ? SourceMaterial->GetName() : "Default"));
		int SurfaceGroup = I;
		for (int Previous = 0; Previous < I; ++Previous)
		{
			if (UV.Set == Surfaces[Previous].Set && Near(UV.Transform, Surfaces[Previous].Transform))
			{
				SurfaceGroup = SurfaceGroups[Previous];
				break;
			}
		}
		Surfaces.push_back(UV);
		SurfaceGroups.push_back(SurfaceGroup);
		int Index = -1;
		for (int M = 0; M < Asset.MaterialKeys.Num(); ++M)
		{
			if (Asset.MaterialKeys[M] == Key)
			{
				Index = M;
				break;
			}
		}
		if (Index < 0)
		{
			Index = Asset.MaterialKeys.Num();
			Asset.MaterialKeys.Add(Key);
			// 모든 LOD는 애셋 단위 슬롯 인덱스를 사용한다.
			for (auto& Existing : Asset.LODs)
			{
				FStaticMaterialSlot Slot;
				Slot.Name = SourceMaterial ? SourceMaterial->GetName() : "Default";
				Existing.MeshData.MaterialSlots.Add(std::move(Slot));
			}
		}
		MaterialMap.push_back(static_cast<uint32>(Index));
	}
	const int CPCount = Item.Mesh->GetControlPointsCount();
	std::vector<std::map<uint16, float>> Influences(Rig ? CPCount : 0);
	if (Rig)
	{
		if (Item.Mesh->GetDeformerCount(FbxDeformer::eSkin))
		{
			auto* Skin = static_cast<FbxSkin*>(Item.Mesh->GetDeformer(0, FbxDeformer::eSkin));
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				if (!Cluster || !Cluster->GetLink() || IsCompensation(Cluster))
				{
					continue;
				}
				if (!Rig->Indices.contains(Cluster->GetLink()))
				{
					Error = "Bone is not in the mesh reference skeleton";
					return false;
				}
				if (Cluster->GetLinkMode() == FbxCluster::eAdditive)
				{
					Warn("Additive Link Mode is approximated using normalized Linear weights: " + NodePath(Item.Node));
				}
				const uint16 Bone = Rig->Indices.at(Cluster->GetLink());
				for (int I = 0; I < Cluster->GetControlPointIndicesCount(); ++I)
				{
					const int CP = Cluster->GetControlPointIndices()[I];
					const double W = Cluster->GetControlPointWeights()[I];
					if (CP < 0 || CP >= CPCount || !std::isfinite(W))
					{
						Error = "Invalid control point influence";
						return false;
					}
					if (W > 0)
					{
						Influences[CP][Bone] = std::max(Influences[CP][Bone], static_cast<float>(W));
					}
				}
			}
		}
		else
		{
			const uint16 Bone = Rig->Indices.at(Item.Node);
			for (auto& Vertex : Influences)
			{
				Vertex[Bone] = 1;
			}
		}
	}
	std::unordered_map<FCornerKey, uint32, FCornerHash> Cache;
	int32 PositionBase = 0;
	for (const auto& SourceVertex : Mesh.LODSourceVertices)
	{
		PositionBase = std::max(PositionBase, SourceVertex.PositionIndex + 1);
	}
	const int32 AttributeBase = Mesh.Vertices.Num();
	std::map<std::array<float, 2>, int32> UVIndices;
	std::map<std::array<float, 3>, int32> NormalIndices;
	FbxGeometryElementMaterial* MaterialsElement = Item.Mesh->GetElementMaterial();
	for (int P = 0; P < Item.Mesh->GetPolygonCount(); ++P)
	{
		if (Item.Mesh->GetPolygonSize(P) != 3)
		{
			Error = "FBX triangulation did not produce triangles";
			return false;
		}
		int Slot = 0;
		if (MaterialsElement && MaterialsElement->GetIndexArray().GetCount())
		{
			const int Index = MaterialsElement->GetMappingMode() == FbxLayerElement::eByPolygon ? P : 0;
			if (Index < MaterialsElement->GetIndexArray().GetCount())
			{
				Slot = MaterialsElement->GetIndexArray().GetAt(Index);
			}
		}
		if (Slot < 0 || Slot >= static_cast<int>(MaterialMap.size()))
		{
			Warn("Invalid material slot replaced with the first slot: " + NodePath(Item.Node));
			Slot = 0;
		}
		const auto& Surface = Surfaces[Slot];
		FVertex Vertices[3];
		int CPs[3];
		bool MappedUVs[3]{};
		for (int C = 0; C < 3; ++C)
		{
			const int CP = Item.Mesh->GetPolygonVertex(P, C);
			CPs[C] = CP;
			if (CP < 0 || CP >= CPCount)
			{
				Error = "Invalid polygon control point index";
				return false;
			}
			auto& V = Vertices[C];
			V.Tangent = FVector4(0, 0, 0, 0);
			V.Position = Reflect(Full.MultT(Item.Mesh->GetControlPointAt(CP))) * static_cast<float>(UnitScale);
			FbxVector4 N;
			if (Item.Mesh->GetPolygonVertexNormal(P, C, N))
			{
				V.Normal = NormalMatrix.TransformVector(FVector(static_cast<float>(N[0]), static_cast<float>(N[1]), static_cast<float>(N[2]))).GetSafeNormal();
			}
			if (!Surface.Set.empty())
			{
				FbxVector2 UV;
				bool Unmapped = false;
				if (Item.Mesh->GetPolygonVertexUV(P, C, Surface.Set.c_str(), UV, Unmapped) && !Unmapped)
				{
					UV = Surface.Transform.Apply(UV);
					MappedUVs[C] = true;
					V.UV = FVector2(static_cast<float>(UV[0]), 1.f - static_cast<float>(UV[1]));
				}
				else
				{
					Warn("Unmapped UV corner: " + NodePath(Item.Node) + ", selected=" + Surface.Set + "; zero UV used");
				}
			}
			if (auto* Colors = Item.Mesh->GetElementVertexColor())
			{
				int Index = -1;
				switch (Colors->GetMappingMode())
				{
				case FbxLayerElement::eByControlPoint:
					Index = CP;
					break;
				case FbxLayerElement::eByPolygonVertex:
					Index = Item.Mesh->GetPolygonVertexIndex(P) + C;
					break;
				case FbxLayerElement::eByPolygon:
					Index = P;
					break;
				case FbxLayerElement::eAllSame:
					Index = 0;
					break;
				default:
					break;
				}
				if (Colors->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					Index = Index >= 0 && Index < Colors->GetIndexArray().GetCount() ? Colors->GetIndexArray().GetAt(Index) : -1;
				}
				if (Index >= 0 && Index < Colors->GetDirectArray().GetCount())
				{
					const FbxColor Color = Colors->GetDirectArray().GetAt(Index);
					V.Color = FVector4(static_cast<float>(Color.mRed), static_cast<float>(Color.mGreen), static_cast<float>(Color.mBlue), static_cast<float>(Color.mAlpha));
				}
			}
		}
		FVector FaceNormal = (Vertices[1].Position - Vertices[0].Position).Cross(Vertices[2].Position - Vertices[0].Position).GetSafeNormal();
		if (Reverse)
		{
			FaceNormal = FaceNormal * -1.f;
		}
		uint32 Corners[3];
		for (int C = 0; C < 3; ++C)
		{
			if (Vertices[C].Normal.SizeSquared() < 1.e-12f)
			{
				Vertices[C].Normal = FaceNormal;
			}
			if (MappedUVs[C] && !ImportTangent(Surface, Item.Mesh, P, C, Transform, Vertices[C]) && Surface.Tangents)
			{
				Warn("Invalid source tangent basis: " + NodePath(Item.Node) + ", UV set=" + Surface.Set + "; affected tangents recalculated");
			}
			const auto& V = Vertices[C];
			if (!std::isfinite(V.Position.X) || !std::isfinite(V.Position.Y) || !std::isfinite(V.Position.Z) ||
				!std::isfinite(V.Normal.X) || !std::isfinite(V.Normal.Y) || !std::isfinite(V.Normal.Z) ||
				!std::isfinite(V.UV.X) || !std::isfinite(V.UV.Y))
			{
				Error = "Invalid FBX vertex attribute";
				return false;
			}
			const FCornerKey Key{ CPs[C], SurfaceGroups[Slot], Vertices[C] };
			if (auto It = Cache.find(Key); It != Cache.end())
			{
				Corners[C] = It->second;
				continue;
			}
			Corners[C] = Mesh.Vertices.Num();
			Mesh.Vertices.Add(Vertices[C]);
			Cache.emplace(Key, Corners[C]);
			// 기존 스태틱 LOD 생성기에 필요한 원본 위치/이음매 식별자다.
			const auto UV = UVIndices.try_emplace({ V.UV.X, V.UV.Y }, AttributeBase + static_cast<int32>(UVIndices.size())).first->second;
			const auto Normal = NormalIndices.try_emplace({ V.Normal.X, V.Normal.Y, V.Normal.Z }, AttributeBase + static_cast<int32>(NormalIndices.size())).first->second;
			Mesh.LODSourceVertices.Add({ PositionBase + CPs[C], UV, Normal });
			if (Rig)
			{
				std::vector<std::pair<uint16, float>> Weights(Influences[CPs[C]].begin(), Influences[CPs[C]].end());
				std::sort(Weights.begin(), Weights.end(), [](auto A, auto B)
					{ return A.second != B.second ? A.second > B.second : A.first < B.first; });
				if (Weights.size() > 4)
				{
					Warn("Weights reduced to four influences: " + NodePath(Item.Node));
				}
				Weights.resize(std::min<size_t>(Weights.size(), 4));
				std::array<uint16, 4> Bones{};
				std::array<float, 4> Values{};
				float Total = 0;
				for (auto W : Weights)
				{
					Total += W.second;
				}
				if (Total <= 0)
				{
					Values[0] = 1;
					Warn("Unweighted vertices attached to root: " + NodePath(Item.Node));
				}
				else
				{
					for (size_t I = 0; I < Weights.size(); ++I)
					{
						Bones[I] = Weights[I].first;
						Values[I] = Weights[I].second / Total;
					}
				}
				LOD.SkinWeights.InfluenceBones.Add(Bones);
				LOD.SkinWeights.InfluenceWeights.Add(Values);
			}
		}
		auto& Indices = Sections[MaterialMap[Slot]];
		Indices.push_back(Corners[0]);
		Indices.push_back(Corners[Reverse ? 2 : 1]);
		Indices.push_back(Corners[Reverse ? 1 : 2]);
	}
	return true;
}

void FImport::LODThresholds(const FOutputGroup& Group, FFbxAssetData& Asset)
{
	const auto& Box = Asset.LODs[0].MeshData.AABB;
	const float Radius = (Box.Max - Box.Min).Size() * .5f;
	std::set<FbxNode*> Seen;
	bool Set[3]{};
	for (auto* Item : Group.Items)
	{
		if (!Item->LODGroupNode || !Seen.insert(Item->LODGroupNode).second)
		{
			continue;
		}
		FbxLODGroup* LOD = nullptr;
		for (int A = 0; A < Item->LODGroupNode->GetNodeAttributeCount(); ++A)
		{
			if (Item->LODGroupNode->GetNodeAttributeByIndex(A)->GetAttributeType() == FbxNodeAttribute::eLODGroup)
			{
				LOD = static_cast<FbxLODGroup*>(Item->LODGroupNode->GetNodeAttributeByIndex(A));
			}
		}
		if (!LOD)
		{
			continue;
		}
		if (LOD->MinMaxDistance.Get())
		{
			Warn("LOD Min/MaxDistance visibility is not imported: " + NodePath(Item->LODGroupNode));
		}
		for (int I = 0; I < std::min(3, LOD->GetNumThresholds()); ++I)
		{
			float Threshold = 0;
			if (LOD->ThresholdsUsedAsPercentage.Get())
			{
				FbxDouble Percentage = 0;
				if (LOD->GetThreshold(I, Percentage))
				{
					Threshold = static_cast<float>(Percentage / 100.);
				}
			}
			else
			{
				FbxDistance Distance;
				if (LOD->GetThreshold(I, Distance))
				{
					// FbxDistance는 자체 단위를 갖는다. 장면 단위를 중복 곱하지 않는다.
					const double D = Distance.valueAs(FbxSystemUnit::m);
					if (D > 0)
					{
						Threshold = static_cast<float>(Radius * 1.7320508075688772 / D);
					}
				}
			}
			if (Threshold > 0 && std::isfinite(Threshold))
			{
				Asset.ScreenThresholds[I] = Set[I] ? std::min(Asset.ScreenThresholds[I], Threshold) : Threshold;
				Set[I] = true;
			}
		}
	}
	for (int I = 1; I < 3; ++I)
	{
		if (Asset.ScreenThresholds[I] >= Asset.ScreenThresholds[I - 1])
		{
			Asset.ScreenThresholds[I] = Asset.ScreenThresholds[I - 1] * .5f;
		}
	}
}

bool FImport::CookMesh(const FOutputGroup& Group)
{
	const bool Skinned = Group.Root != nullptr;
	const auto Type = Skinned ? EFbxAssetType::SkeletalMesh : EFbxAssetType::StaticMesh;
	const FString Key = MakeKey(Skinned ? "SK" : "SM", Group.Label, Group.Identity);
	if (Key.empty())
	{
		return false;
	}
	auto Asset = Data(Type, Key, Group.Identity);
	// 이름이 _LOD1인 메시만 있는 FBX처럼 LOD0이 없으면 가장 낮은 단계를 LOD0으로 당긴다.
	int MaxLOD = 0, MinLOD = 4;
	for (auto* Item : Group.Items)
	{
		MaxLOD = std::max(MaxLOD, Item->LOD);
		if (Item->LOD >= 0)
		{
			MinLOD = std::min(MinLOD, Item->LOD);
		}
	}
	const int FirstLOD = MinLOD < 4 ? MinLOD : 0;
	Asset.LODs.SetNum(std::min(4, MaxLOD - FirstLOD + 1));
	if (Skinned)
	{
		const auto& Rig = Rigs.at(NodePath(Group.Root));
		Asset.RefSkeleton = Rig.Reference;
		Asset.SkeletonKey = Rig.Key;
	}
	for (int L = 0; L < Asset.LODs.Num(); ++L)
	{
		std::map<uint32, std::vector<uint32>> Sections;
		// 그룹 밖 조각은 모든 단계에 포함한다. 그룹의 누락 LOD는 마지막 기하를 반복하지 않는다.
		for (auto* Item : Group.Items)
		{
			if (Item->LOD < 0 || Item->LOD - FirstLOD == L)
			{
				if (!AppendMesh(*Item, Asset, L, Sections))
				{
					return false;
				}
			}
		}
		auto& Mesh = Asset.LODs[L].MeshData;
		for (const auto& [MaterialIndex, Indices] : Sections)
		{
			Mesh.Sections.Add({ static_cast<uint32>(Mesh.Indices.Num()), static_cast<uint32>(Indices.size()), MaterialIndex });
			for (auto Index : Indices)
			{
				Mesh.Indices.Add(Index);
			}
		}
		if (!Mesh.Vertices.IsEmpty())
		{
			std::vector<std::pair<int32, FVector4>> SourceTangents;
			for (int32 I = 0; I < Mesh.Vertices.Num(); ++I)
			{
				if (Mesh.Vertices[I].Tangent.W != 0)
				{
					SourceTangents.emplace_back(I, Mesh.Vertices[I].Tangent);
				}
			}
			if (SourceTangents.size() != static_cast<size_t>(Mesh.Vertices.Num()))
			{
				Mesh.BuildTangentSpace();
				for (const auto& [Index, Tangent] : SourceTangents)
				{
					Mesh.Vertices[Index].Tangent = Tangent;
				}
			}
			FVector Min = Mesh.Vertices[0].Position, Max = Min;
			for (const auto& V : Mesh.Vertices)
			{
				Min.X = std::min(Min.X, V.Position.X);
				Min.Y = std::min(Min.Y, V.Position.Y);
				Min.Z = std::min(Min.Z, V.Position.Z);
				Max.X = std::max(Max.X, V.Position.X);
				Max.Y = std::max(Max.Y, V.Position.Y);
				Max.Z = std::max(Max.Z, V.Position.Z);
			}
			Mesh.AABB = FBox{ Min, Max };
		}
	}
	if (Asset.LODs[0].MeshData.Vertices.IsEmpty())
	{
		Error = "No visible LOD0 geometry: " + Group.Label;
		return false;
	}
	LODThresholds(Group, Asset);
	if (!Asset.Validate(Error))
	{
		return false;
	}
	Output.Add(std::move(Asset));
	return true;
}

bool FImport::Run()
{
	//메시 모으기
	Gather(Scene->GetRootNode(), true);

	if (!Options.TargetSkeletonKey.empty())
	{
		TargetSkeleton = UAssetManager::GetAssetByPath<USkeleton>(Options.TargetSkeletonKey);
		if (!TargetSkeleton)
		{
			Error = "Target skeleton is not loaded: " + Options.TargetSkeletonKey;
			return false;
		}
	}

	if (Meshes.empty() && !TargetSkeleton)
	{
		Error = "No visible supported mesh geometry in FBX";
		return false;
	}
	if (!Options.bImportStaticMeshes && !Options.bImportSkeletalMeshes && !TargetSkeleton)
	{
		Error = "Select Static or Skeletal mesh import";
		return false;
	}

	// 리그를 찾지 않으면 스킨 메시도 Static 경로로 들어간다.
	if (Options.bImportSkeletalMeshes || TargetSkeleton)
	{
		//본 찾기
		FindRigs();

		if (!Error.empty())
		{
			return false;
		}
	}
	for (const auto& Item : Meshes)
	{
		if (!TargetSkeleton && !Item.Rig && MovesWithAnimation(Scene, Item.Node))
		{
			Warn("Transform animation is ignored; enable Import Skeletal Meshes: " + NodePath(Item.Node));
		}
	}
	if (!Options.bImportStaticMeshes && !TargetSkeleton)
	{
		std::erase_if(Meshes, [](const FMeshItem& Item)
			{ return !Item.Rig; });
		if (Meshes.empty())
		{
			Error = "No skinned mesh in FBX; enable Static Mesh import";
			return false;
		}
	}
	PreparePoses();
	// 메시가 연결되는 리그만 출력한다. 포즈/컨트롤 본은 실제 본 테이블에 유지된다.
	std::set<FbxNode*> Used;
	for (const auto& Item : Meshes)
	{
		if (Item.Rig)
		{
			Used.insert(Item.Rig);
		}
	}
	//메시가 쓰는 리그만 BuildRig로 만든다.
	for (auto& [Path, Rig] : Rigs)
	{
		if ((TargetSkeleton || Used.contains(Rig.Root)) && !BuildRig(Rig))
		{
			return false;
		}
	}
	// Target Skeleton 모드는 애니메이션만 만든다. 리그는 위에서 대상과 맞춰 봤으므로 메시는 더 쓰지 않는다.
	if (TargetSkeleton)
	{
		Meshes.clear();
	}
	std::map<FString, FOutputGroup> Groups;
	for (auto& Item : Meshes)
	{
		const bool Combine = Item.Rig ? Options.bCombineSkeletalMeshes : Options.bCombineStaticMeshes;
		const FString Identity = Item.Rig ? "Skeletal:" + NodePath(Item.Rig) + (Combine ? "Combined" : Item.UnitKey)
										  : "Static:" + (Combine ? FString("Combined") : Item.UnitKey);
		auto& Group = Groups[Identity];
		Group.Identity = Identity;
		Group.Root = Item.Rig;
		Group.Label = Combine ? (Item.Rig ? FString(Item.Rig->GetName()) : FString("Combined")) : Item.Label;
		Group.Items.push_back(&Item);
	}
	for (const auto& [Key, Group] : Groups)
	{
		if (!CookMesh(Group))
		{
			return false;
		}
	}
	// 애니메이션은 출력된 Skeleton(메시가 쓰는 리그)마다 만든다.
	if ((Options.bImportSkeletalMeshes && Options.bImportAnimations) || TargetSkeleton)
	{
		bool bMatchedRig = false;
		for (const auto& [Path, Rig] : Rigs)
		{
			if (Rig.Key.empty())
			{
				continue;
			}
			bMatchedRig = true;
			if (!CookAnimations(Rig))
			{
				return false;
			}
		}
		if (TargetSkeleton && !bMatchedRig)
		{
			Error = "No skeleton in the FBX matches the target skeleton: " + Options.TargetSkeletonKey;
			return false;
		}
		if (TargetSkeleton && Output.IsEmpty())
		{
			Error = "The FBX has no animation that moves a bone of the target skeleton";
			return false;
		}
	}
	std::set<FString> ReferencedTextures;
	for (const auto& Asset : Output)
	{
		if (Asset.Type == EFbxAssetType::Material)
		{
			ReferencedTextures.insert(Asset.Material.DiffuseTexture);
			ReferencedTextures.insert(Asset.Material.NormalTexture);
		}
	}
	for (int32 I = Output.Num() - 1; I >= 0; --I)
	{
		if (Output[I].Type == EFbxAssetType::Texture2D && !ReferencedTextures.contains(Output[I].Key))
		{
			Output.RemoveAt(I);
		}
	}
	for (const auto& Asset : Output)
	{
		if (!Asset.Validate(Error))
		{
			return false;
		}
	}
	return true;
}

struct FManagerDelete
{
	void operator()(FbxManager* P) const
	{
		if (P)
		{
			P->Destroy();
		}
	}
};

// 메시와 애니메이션이 같은 축과 단위로 읽히도록 씬 준비를 한곳에 둔다.
struct FLoadedScene
{
	std::unique_ptr<FbxManager, FManagerDelete> Manager;
	FbxScene* Scene = nullptr;
	double UnitScale = 1;
	bool bBlender = false;
};

bool LoadScene(const fs::path& Source, const FFbxImportOptions& Options, const fs::path* Extraction, FLoadedScene& Out, FString& Error)
{
	Out.Manager.reset(FbxManager::Create());
	if (!Out.Manager)
	{
		Error = "Cannot create FBX SDK manager";
		return false;
	}
	auto* IOSettings = FbxIOSettings::Create(Out.Manager.get(), IOSROOT);
	Out.Manager->SetIOSettings(IOSettings);
	// 추출 폴더가 없으면 텍스처가 필요 없는 읽기다. SDK가 FBX 옆에 .fbm을 풀지 않게 끈다.
	if (!Extraction)
	{
		IOSettings->SetBoolProp(IMP_FBX_MATERIAL, false);
		IOSettings->SetBoolProp(IMP_FBX_TEXTURE, false);
		IOSettings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
	}
	FbxImporter* Parser = FbxImporter::Create(Out.Manager.get(), "");
	const FString CanonicalSource = FbxPath::ToString(Source);
	if (!Parser->Initialize(CanonicalSource.c_str(), -1, IOSettings))
	{
		Error = Parser->GetStatus().GetErrorString();
		return false;
	}
	if (Extraction)
	{
		Parser->SetEmbeddingExtractionFolder(FbxPath::ToString(*Extraction).c_str());
	}
	Out.Scene = FbxScene::Create(Out.Manager.get(), "Import");
	if (!Parser->Import(Out.Scene))
	{
		Error = Parser->GetStatus().GetErrorString();
		return false;
	}
	if (auto* Info = Out.Scene->GetSceneInfo())
	{
		Out.bBlender = Lower(Info->Original_ApplicationName.Get().Buffer()).find("blender") != FString::npos;
	}
	Out.UnitScale = Out.Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor() / 100. * Options.UniformScale;
	if (!std::isfinite(Out.UnitScale) || Out.UnitScale <= 0)
	{
		Error = "Invalid FBX scene unit or uniform scale";
		return false;
	}
	// UE의 Z-up 기준을 사용한다. Deep 변환으로 Bake off의 로컬 기하도 같은 축이 된다.
	const auto Front = Options.bForceFrontX ? FbxAxisSystem::eParityEven : static_cast<FbxAxisSystem::EFrontVector>(-FbxAxisSystem::eParityOdd);
	FbxAxisSystem Target(FbxAxisSystem::eZAxis, Front, FbxAxisSystem::eRightHanded);
	Target.DeepConvertScene(Out.Scene);
	return true;
}
}
#endif

bool FFbxAssetImporter::IsAvailable() { return WITH_FBX_SDK != 0; }

bool FFbxAssetImporter::Cook(const FString& SourcePath, const FFbxImportOptions& Options, const FString& ExtractionDirectory,
	TArray<FFbxAssetData>& Out, TArray<FString>& Warnings, FString& Error, const FString& OutputDirectory, const FString& OwnerSource)
{
	Out.Empty();
	Warnings.Empty();
	Error.clear();
#if WITH_FBX_SDK
	try
	{
		FString ActualKey;
		fs::path Source, Destination, Extraction;
		if (!FbxPath::GetSourcePath(SourcePath, Source, ActualKey, Error) ||
			!FbxPath::GetOutputDirectory(OutputDirectory.empty() ? FbxPath::DefaultOutputDirectory(SourcePath) : OutputDirectory, Destination, Error) ||
			!FbxPath::GetOutputDirectory(ExtractionDirectory, Extraction, Error))
		{
			return false;
		}
		// 애니메이션은 대상 Skeleton을 만들 때와 같은 공간으로 읽어야 한다. 루트 로컬이 Bake 설정에, 본 이동이 스케일에, 축이 Front X에 따라 달라진다.
		FFbxImportOptions Effective = Options;
		if (!Options.TargetSkeletonKey.empty())
		{
			fs::path SkeletonFile;
			FFbxAssetData SkeletonHeader;
			if (!FbxPath::ResolveKey(Options.TargetSkeletonKey, SkeletonFile, Error) ||
				!FFbxAssetBake::ReadHeader(FbxPath::ToString(SkeletonFile), SkeletonHeader, Error))
			{
				return false;
			}
			const auto& Made = SkeletonHeader.Options;
			if (Made.bBakeSkeletalMeshes != Options.bBakeSkeletalMeshes || Made.bForceFrontX != Options.bForceFrontX || Made.UniformScale != Options.UniformScale)
			{
				Warnings.Add("Transform options follow the target skeleton's import (Bake Meshes, Force Front X Axis, Uniform Scale)");
			}
			Effective.bBakeSkeletalMeshes = Made.bBakeSkeletalMeshes;
			Effective.bForceFrontX = Made.bForceFrontX;
			Effective.UniformScale = Made.UniformScale;
		}
		FLoadedScene Loaded;
		if (!LoadScene(Source, Effective, &Extraction, Loaded, Error))

		{
			return false;
		}
		FbxGeometryConverter Converter(Loaded.Manager.get());
		if (!Converter.Triangulate(Loaded.Scene, true))
		{
			Error = "FBX SDK triangulation failed";
			return false;
		}
		FImport Import(Loaded.Scene, Effective, OwnerSource.empty() ? ActualKey : OwnerSource, Source, Destination, Out, Warnings, Error);
		Import.ActualSourceKey = ActualKey;
		Import.SourceHash = FbxPath::FileHash(Source);
		Import.UnitScale = Loaded.UnitScale;
		Import.bBlender = Loaded.bBlender;
		const bool Result = Import.Run();
		if (!Result)
		{
			Out.Empty();
		}
		return Result;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		Out.Empty();
		return false;
	}
#else
	Error = "FBX SDK is not configured. Set FBX_SDK_ROOT and regenerate the solution; existing .fbxbin assets can still be loaded.";
	return false;
#endif
}
