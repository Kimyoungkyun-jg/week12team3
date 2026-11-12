#include "EnginePCH.h"
#include "FbxImporter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Asset/AssetManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <numeric>
#include <limits>

namespace
{
	constexpr int32 DefaultAnimationSampleRate = 30;
	constexpr int32 MaxAnimationReferenceRate = 1920;
	constexpr double AnimationTimePrecision = 1.e-4;

	int32 GetTimeSampleRate(double DeltaTime)
	{
		const double OriginalRate = 1.0 / DeltaTime;
		const double Precision = std::clamp(AnimationTimePrecision * OriginalRate, AnimationTimePrecision, 0.1);
		double Rate = OriginalRate;
		double Fraction = Rate - std::floor(Rate);
		int32 Multiplier = 2;
		while (Fraction > Precision && 1.0 - Fraction > Precision)
		{
			Rate = OriginalRate * Multiplier;
			if (Rate > MaxAnimationReferenceRate)
				return DefaultAnimationSampleRate;

			Fraction = Rate - std::floor(Rate);
			++Multiplier;
		}
		return static_cast<int32>(std::min(std::round(Rate), static_cast<double>(MaxAnimationReferenceRate)));
	}

	int32 GetAnimationCurveRate(FbxAnimCurve* Curve)
	{
		if (!Curve || Curve->KeyGetCount() < 2) return 0;
		FbxTimeSpan Interval;
		if (!Curve->GetTimeInterval(Interval)) return 0;
		const double Duration = Interval.GetDuration().GetSecondDouble();
		if (!std::isfinite(Duration) || Duration <= 0.0) return 0;

		const double AverageRate = std::round((Curve->KeyGetCount() - 1) / Duration);
		if (AverageRate >= DefaultAnimationSampleRate)
		{
			if (AverageRate > (std::numeric_limits<int32>::max)())
				return MaxAnimationReferenceRate;
			return static_cast<int32>(AverageRate);
		}

		int32 SampleRate = 1;
		double PreviousTime = 0.0;
		TSet<int32> ProcessedDeltas;
		for (int32 KeyIndex = 0; KeyIndex < Curve->KeyGetCount(); ++KeyIndex)
		{
			const double Time = Curve->KeyGetTime(KeyIndex).GetSecondDouble();
			const double Delta = Time < 0.0 && KeyIndex == 0 ? 0.0 : Time - PreviousTime;
			const double Fraction = Delta - std::trunc(Delta);
			PreviousTime = Time;
			const int32 DeltaKey = static_cast<int32>(std::round(Fraction / AnimationTimePrecision));
			if (Fraction <= AnimationTimePrecision || ProcessedDeltas.Contains(DeltaKey)) continue;

			ProcessedDeltas.Add(DeltaKey);
			const int32 DeltaRate = GetTimeSampleRate(Fraction);
			SampleRate = static_cast<int32>(std::min<int64>(
				std::lcm<int64>(SampleRate, DeltaRate), MaxAnimationReferenceRate));
		}
		return SampleRate;
	}

	void GetNodeSampleRate(FbxNode* Node, FbxAnimLayer* Layer, TSet<int32>& OutRates)
	{
		const std::array<FbxAnimCurve*, 9> Curves = {
			Node->LclTranslation.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_X, false),
			Node->LclTranslation.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Y, false),
			Node->LclTranslation.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Z, false),
			Node->LclRotation.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_X, false),
			Node->LclRotation.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Y, false),
			Node->LclRotation.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Z, false),
			Node->LclScaling.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_X, false),
			Node->LclScaling.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Y, false),
			Node->LclScaling.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Z, false)
		};
		for (FbxAnimCurve* Curve : Curves)
		{
			const int32 Rate = GetAnimationCurveRate(Curve);
			if (Rate > 0) OutRates.Add(Rate);
		}
	}

	// Node Geometry를 이어 붙이며 정점/Index/Material Slot 참조와 경계를 보정한다.
	void AppendGeometry(const FStaticMeshData& Mesh, FStaticMeshData& Result)
	{
		if (Mesh.Vertices.IsEmpty()) return;
		const uint32 VertexBase = static_cast<uint32>(Result.Vertices.Num());
		const uint32 IndexBase = static_cast<uint32>(Result.Indices.Num());
		const uint32 SlotBase = static_cast<uint32>(Result.MaterialSlots.Num());
		if (Result.Vertices.IsEmpty()) Result.AABB = Mesh.AABB;
		else Result.AABB.Expand(Mesh.AABB);
		Result.Vertices.Append(Mesh.Vertices);
		for (uint32 Index : Mesh.Indices) Result.Indices.Add(VertexBase + Index);
		Result.MaterialSlots.Append(Mesh.MaterialSlots);
		for (const FStaticMeshSection& Section : Mesh.Sections)
			Result.Sections.Add({ IndexBase + Section.StartIndex, Section.IndexCount, SlotBase + Section.MaterialSlotIndex });
	}

	// Cluster의 Bone별 Weight를 Control Point별로 모으고, 동일 Bone 합산 후 상위 Influence를 정규화한다.
	bool ReadControlPointWeights(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndices, TArray<FSkinWeight>& Result, TArray<FString>& Warnings, FString& Error)
	{
		FbxMesh* Mesh = Node->GetMesh();
		const int32 Count = Mesh->GetControlPointsCount();
		TArray<TMap<int32, double>> Influences;
		Influences.SetNum(Count);
		bool bWarnedLinkMode = false;
		int64 SkippedNegativeWeights = 0;
		for (int S = 0; S < Mesh->GetDeformerCount(FbxDeformer::eSkin); ++S)
		{
			auto* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(S, FbxDeformer::eSkin));
			if (Skin->GetSkinningType() == FbxSkin::eDualQuaternion || Skin->GetSkinningType() == FbxSkin::eBlend)
			{
				// TODO: 현재는 Dual Quaternion/Blend도 Linear Skinning으로 처리하여 원본의 변형 방식을 보존하지 못한다.
				// 해당 변형 방식에 필요한 데이터를 출력에 보존하고, 런타임 Skinning도 이를 사용하도록 확장해야 한다.
				Warnings.Add("FBX: Dual Quaternion/Blend skin imported as Linear: " + FString(Node->GetName()));
			}
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				const int32* BoneIndex = BoneIndices.Find(Cluster->GetLink());
				if (!BoneIndex) { Error = "FBX: weight Cluster Link is outside skeleton: " + FString(Node->GetName()); return false; }
				if (Cluster->GetLinkMode() != FbxCluster::eNormalize && !bWarnedLinkMode)
				{
					Warnings.Add("FBX: TotalOne/Additive weights normalized for Linear skinning: " + FString(Node->GetName()));
					bWarnedLinkMode = true;
				}
				const int Entries = Cluster->GetControlPointIndicesCount();
				const int* Points = Cluster->GetControlPointIndices();
				const double* Weights = Cluster->GetControlPointWeights();
				if (Entries < 0 || (Entries > 0 && (!Points || !Weights))) { Error = "FBX: invalid Cluster weight arrays: " + FString(Node->GetName()); return false; }
				for (int I = 0; I < Entries; ++I)
				{
					const int CP = Points[I];
					const double Weight = Weights[I];
					if (CP < 0 || CP >= Count || !std::isfinite(Weight)) { Error = "FBX: invalid Control Point index or weight: " + FString(Node->GetName()); return false; }
					if (Weight < 0)
					{
						++SkippedNegativeWeights;
						continue;
					}
					if (Weight == 0) continue;
					double& Sum = Influences[CP].FindOrAdd(*BoneIndex);
					Sum += Weight;
					if (!std::isfinite(Sum)) { Error = "FBX: weight sum overflow: " + FString(Node->GetName()); return false; }
				}
			}
		}
		if (SkippedNegativeWeights > 0)
			Warnings.Add("FBX: skipped " + std::to_string(SkippedNegativeWeights) + " negative skin weights: " + FString(Node->GetName()));

		Result.SetNum(Count);
		for (int32 CP = 0; CP < Count; ++CP)
		{
			TArray<std::pair<int32, double>> Sorted;
			for (const auto& Influence : Influences[CP]) Sorted.Add({ Influence.first, Influence.second });
			if (Sorted.IsEmpty())
			{
				// TODO: 현재는 양수 Weight가 없는 Control Point가 하나라도 있으면 Import 전체가 실패한다.
				// 이런 정점의 처리 정책을 먼저 정하고, 임의 Bone에 귀속하지 않으면서 Import를 계속할 대체 처리를 추가해야 한다.
				Error = "FBX: Control Point has no positive influence: " + FString(Node->GetName()) + " [" + std::to_string(CP) + "]";
				return false;
			}
			// 동률은 Bone 번호로 정렬하여 TMap 순회 순서와 관계없이 같은 결과를 만든다.
			std::sort(Sorted.begin(), Sorted.end(), [](const auto& A, const auto& B)
			{
				return A.second != B.second ? A.second > B.second : A.first < B.first;
			});
			const int32 Used = std::min(MaxBoneInfluences, Sorted.Num());
			double Sum = 0;
			for (int32 I = 0; I < Used; ++I) Sum += Sorted[I].second;
			if (!std::isfinite(Sum)) { Error = "FBX: weight sum overflow: " + FString(Node->GetName()); return false; }
			FSkinWeight Weight;
			for (int32 I = 0; I < Used; ++I)
			{
				Weight.BoneIndex[I] = static_cast<uint8>(Sorted[I].first);
				Weight.Weight[I] = static_cast<float>(Sorted[I].second / Sum);
			}
			Result[CP] = Weight;
		}
		return true;
	}

	bool IsBoneNode(FbxNode* Node)
	{
		const FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		if (!Attribute) return false;
		const auto Type = Attribute->GetAttributeType();
		return Type == FbxNodeAttribute::eSkeleton || Type == FbxNodeAttribute::eNull || Type == FbxNodeAttribute::eMesh;
	}

	FbxNode* FindSkeletonRoot(FbxNode* Node, FbxScene* Scene, bool bIsBlender)
	{
		while (FbxNode* Parent = Node->GetParent())
		{
			if (Parent == Scene->GetRootNode() || !IsBoneNode(Parent)) break;
			const auto Type = Parent->GetNodeAttribute()->GetAttributeType();
			// Blender의 Armature 컨테이너는 Bone에서 제외한다. 그 Transform은 Root의 Global에 남는다.
			// if (bIsBlender && Type == FbxNodeAttribute::eNull && Parent->GetParent() == Scene->GetRootNode() && _stricmp(Parent->GetName(), "armature") == 0) break;
			// UE GetRootSkeleton처럼 Skin Mesh가 Bone의 조상인 경우 그 Mesh까지 올라가지 않는다.
			if (Type == FbxNodeAttribute::eMesh && Parent->GetMesh()->GetDeformerCount(FbxDeformer::eSkin) > 0) break;
			Node = Parent;
		}
		return Node;
	}

	void CollectBoneNodes(FbxNode* Node, TArray<FbxNode*>& Nodes)
	{
		if (!IsBoneNode(Node)) return;
		Nodes.Add(Node);
		for (int Child = 0; Child < Node->GetChildCount(); ++Child) CollectBoneNodes(Node->GetChild(Child), Nodes);
	}

	// 이 Bone의 Bind Global을 Bind Pose → Cluster Link → 현재 Global 순서로 찾는다.
	// "Bone missing from Bind Pose"는 파일의 모든 Bind Pose에서 이 Node를 찾지 못했다는 뜻이다.
	// 파일 전체에 Bind Pose가 없거나 Import에 실패했다는 뜻은 아니다. Weight 없는 IK/보조 Bone도
	// Skeleton에 포함하므로, 이런 Bone의 Bind Pose 항목이 원본에 없으면 같은 경고가 발생할 수 있다.
	// Bind Pose에 있으면 저장된 Global 행렬을 사용한다. 없으면 경고를 남긴 뒤 이 Node를 Link로
	// 참조하는 Cluster의 TransformLinkMatrix를 사용하고, 그런 Cluster도 없으면 현재 Global을 사용한다.
	// 현재 Global을 사용하는 경우 원래 Bind 시점의 자세를 복구하는 것은 아니며, 현재 자세를 Bind로 삼는다.
	// Bone 자체는 제외하지 않는다. 호출부가 반환된 Global로 Local Bind와 Inverse Bind를 구성한다.
	FbxAMatrix ReadBindGlobal(FbxScene* Scene, FbxNode* Node, const TArray<FbxCluster*>& Clusters, TArray<FString>& Warnings)
	{
		FbxAMatrix Global;
		for (int I = 0; I < Scene->GetPoseCount(); ++I)
		{
			FbxPose* Pose = Scene->GetPose(I);
			if (!Pose->IsBindPose()) continue;
			const int Index = Pose->Find(Node);
			if (Index < 0) continue;
			const FbxMatrix& Matrix = Pose->GetMatrix(Index);
			std::copy_n(static_cast<const double*>(Matrix), 16, static_cast<double*>(Global));
			return Global;
		}
		Warnings.Add("Bone missing from Bind Pose; using Cluster Link or current Global: " + FString(Node->GetName()));
		for (FbxCluster* Cluster : Clusters)
		{
			if (Cluster->GetLink() != Node) continue;
			Cluster->GetTransformLinkMatrix(Global);
			return Global;
		}
		return Node->EvaluateGlobalTransform();
	}

	bool IsFiniteMatrix(const FbxAMatrix& Matrix)
	{
		const double* Values = Matrix;
		return std::all_of(Values, Values + 16, [](double Value) { return std::isfinite(Value); });
	}

	using FVertexKey = std::pair<int32, std::array<float, 9>>;

	// OBJ Importer와 같은 방식으로 정점 조합의 해시를 만든다.
	struct FVertexKeyHash
	{
		size_t operator()(const FVertexKey& Key) const
		{
			size_t Hash = std::hash<int32>{}(Key.first);
			for (float Value : Key.second)
				Hash = Hash * 31 + std::hash<float>{}(Value);
			return Hash;
		}
	};

	std::filesystem::path PathOf(const FString& Text)
	{
		return std::filesystem::path(std::u8string(Text.begin(), Text.end()));
	}

	void Warn(TArray<FString>& Warnings, const FString& Text)
	{
		if (std::find(Warnings.begin(), Warnings.end(), Text) == Warnings.end()) Warnings.Add(Text);
	}

	FVector ToVector(const FbxVector4& Value)
	{
		return FVector(static_cast<float>(Value[0]), static_cast<float>(Value[1]), static_cast<float>(Value[2]));
	}

	// Mapping은 대상 칸을, Reference는 그 칸이 값인지 값 배열의 Index인지를 결정한다.
	template <typename Element, typename Value>
	bool ReadElement(const Element* Layer, int ControlPoint, int PolygonVertex, int Polygon, Value& OutValue, FString& Error)
	{
		int Index;
		switch (Layer->GetMappingMode())
		{
		case FbxLayerElement::eByControlPoint: Index = ControlPoint; break;
		case FbxLayerElement::eByPolygonVertex: Index = PolygonVertex; break;
		case FbxLayerElement::eByPolygon: Index = Polygon; break;
		case FbxLayerElement::eAllSame: Index = 0; break;
		default: Error = "unsupported layer mapping"; return false;
		}
		if (Layer->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
		{
			if (Index < 0 || Index >= Layer->GetIndexArray().GetCount()) { Error = "layer index array out of range"; return false; }
			Index = Layer->GetIndexArray().GetAt(Index);
		}
		else if (Layer->GetReferenceMode() != FbxLayerElement::eDirect)
		{
			Error = "unsupported layer reference mode";
			return false;
		}
		if (Index < 0 || Index >= Layer->GetDirectArray().GetCount()) { Error = "layer direct array out of range"; return false; }
		OutValue = Layer->GetDirectArray().GetAt(Index);
		return true;
	}

	// SDK 기본 추출은 동일 파일명의 공유 Texture 경로를 잘못 연결할 수 있어 원본 전체 경로로 대응시킨다.
	// Open 중에만 사용하는 상태다. 추출 파일의 수명은 호출자가 관리하는 작업 폴더를 따른다.
	struct FEmbeddedExtraction
	{
		std::filesystem::path Folder;
		TMap<FString, FString> Files;
		FString Error;

		static FString Key(const char* Name)
		{
			const auto Text = PathOf(Name).lexically_normal().generic_u8string();
			return FString(Text.begin(), Text.end());
		}

		static FbxCallback::State Read(void* User, FbxClassId Hint, const char* Name, const void* Data, size_t Size)
		{
			// 현재 Import 대상은 이미지다. 다른 Embedded media는 디스크에 추출하지 않는다.
			if (Hint != FbxVideo::ClassId) return FbxCallback::eHandled;
			auto& State = *static_cast<FEmbeddedExtraction*>(User);
			if (!State.Error.empty()) return FbxCallback::eFailure;
			try
			{
				if (!Name || !*Name || !Data || !Size) { State.Error = "FBX: empty embedded texture"; return FbxCallback::eFailure; }
				const FString Original = Key(Name);
				if (State.Files.Contains(Original)) return FbxCallback::eHandled;
				// 서로 다른 폴더의 동일 파일명도 보존하며, 원본 경로를 추출 대상 경로로 사용하지 않는다.
				const auto Directory = State.Folder / std::to_string(State.Files.Num());
				std::filesystem::create_directory(Directory);
				const auto Target = Directory / PathOf(Name).filename();
				std::ofstream File(Target, std::ios::binary | std::ios::trunc);
				File.write(static_cast<const char*>(Data), static_cast<std::streamsize>(Size));
				File.close();
				if (!File) { State.Error = "FBX: cannot extract embedded texture: " + Original; return FbxCallback::eFailure; }
				const auto Text = Target.generic_u8string();
				State.Files.Add(Original, FString(Text.begin(), Text.end()));
				return FbxCallback::eHandled;
			}
			catch (const std::exception& Exception)
			{
				State.Error = "FBX: embedded texture extraction failed: " + FString(Exception.what());
				return FbxCallback::eFailure;
			}
		}

		void ResolveTextures(FbxScene* Scene) const
		{
			for (int I = 0; I < Scene->GetSrcObjectCount<FbxFileTexture>(); ++I)
			{
				FbxFileTexture* Texture = Scene->GetSrcObject<FbxFileTexture>(I);
				if (const FString* Extracted = Files.Find(Key(Texture->GetFileName()))) Texture->SetFileName(Extracted->c_str());
			}
		}
	};

	// Material의 지정 속성에 연결된 첫 File Texture를 찾아 원본 또는 추출 파일의 절대 경로를 반환한다.
	// 절대 경로 → FBX 폴더+상대 경로 → FBX 폴더+파일명 순으로 탐색하며, 파일 복사/이미지 로딩은 하지 않는다.
	// 연결이 없으면 빈 경로를 반환하고, 파일 누락이나 지원하지 않는 Texture 방식은 경고로 알린다.
	FString ReadTexture(FbxSurfaceMaterial* Material, const char* PropertyName, const std::filesystem::path& Folder, const char* UVName, TArray<FString>& Warnings)
	{
		const FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid()) return {};
		if (Property.GetSrcObjectCount<FbxLayeredTexture>() > 0)
		{
			Warn(Warnings, "Layered texture is unsupported: " + FString(Material->GetName()));
			return {};
		}
		const int Count = Property.GetSrcObjectCount<FbxFileTexture>();
		if (!Count) return {};
		if (Count > 1) Warn(Warnings, "Only the first file texture is used: " + FString(Material->GetName()));
		FbxFileTexture* Texture = Property.GetSrcObject<FbxFileTexture>(0);
		const FString TextureUV = Texture->UVSet.Get().Buffer();
		if (!TextureUV.empty() && TextureUV != "default" && TextureUV != UVName) Warn(Warnings, "Texture requests an unused UV set: " + TextureUV);
		const auto FileName = PathOf(Texture->GetFileName());
		const auto RelativeName = PathOf(Texture->GetRelativeFileName());
		const auto Relative = RelativeName.empty() ? FileName : RelativeName;
		const auto Leaf = FileName.empty() ? RelativeName.filename() : FileName.filename();
		const std::array<std::filesystem::path, 3> Candidates = { FileName.is_absolute() ? FileName : std::filesystem::path(), !Relative.empty() && !Relative.is_absolute() ? Folder / Relative : std::filesystem::path(), Leaf.empty() ? std::filesystem::path() : Folder / Leaf };
		for (const auto& Candidate : Candidates)
		{
			std::error_code EC;
			if (Candidate.empty() || !std::filesystem::is_regular_file(Candidate, EC)) continue;
			const auto Absolute = std::filesystem::absolute(Candidate, EC);
			if (EC) continue;
			const auto Text = Absolute.lexically_normal().generic_u8string();
			return FString(Text.begin(), Text.end());
		}
		Warn(Warnings, "Texture file not found: " + FString(Material->GetName()) + "/" + PropertyName + " : " + Texture->GetFileName());
		return {};
	}

	// Node의 Material 순서를 유지하여 Slot 표를 만들고 이름, Diffuse 색상, Diffuse/Normal Texture 경로를 채운다.
	// NormalMap 경로를 찾지 못하면 Bump를 대신 조회하고, Material이 없는 Node에는 기본 Slot 하나를 만든다.
	// 여기서는 Slot 표만 만들며, 삼각형별 Slot 지정과 Section 구성은 ReadMeshNode에서 처리한다.
	void ReadMaterials(FbxNode* Node, const std::filesystem::path& Folder, const char* UVName, TArray<FStaticMaterialSlot>& Slots, TArray<FString>& Warnings)
	{
		for (int I = 0; I < std::max(1, Node->GetMaterialCount()); ++I)
		{
			FStaticMaterialSlot Slot;
			Slot.Name = "Default";
			FbxSurfaceMaterial* Material = I < Node->GetMaterialCount() ? Node->GetMaterial(I) : nullptr;
			if (Material)
			{
				Slot.Name = Material->GetName();
				const FbxProperty Diffuse = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
				if (Diffuse.IsValid())
				{
					const FbxDouble3 Color = Diffuse.Get<FbxDouble3>();
					Slot.BaseColor = FVector4(static_cast<float>(Color[0]), static_cast<float>(Color[1]), static_cast<float>(Color[2]), 1.0f);
				}
				Slot.DiffuseTexturePath = ReadTexture(Material, FbxSurfaceMaterial::sDiffuse, Folder, UVName, Warnings);
				Slot.NormalTexturePath = ReadTexture(Material, FbxSurfaceMaterial::sNormalMap, Folder, UVName, Warnings);
				if (Slot.NormalTexturePath.empty()) Slot.NormalTexturePath = ReadTexture(Material, FbxSurfaceMaterial::sBump, Folder, UVName, Warnings);
			}
			Slots.Add(std::move(Slot));
		}
	}

	// 삼각형화가 끝난 Mesh 하나에서 엔진 기준 Geometry/Material과 정점별 Control Point 번호를 추출한다.
	// Node와 Skin 유무도 보관하여 후속 Skeleton 분류와 Weight 연결에 사용한다. 여기서는 Bone/Weight를 읽지 않는다.
	bool ReadMeshNode(FbxNode* Node, FbxMesh* Mesh, const FbxAMatrix& Global, const std::filesystem::path& Folder, FFbxMeshNodeData& Result, TArray<FString>& Warnings, FString& Error)
	{
		Result.Node = Node;
		Result.bHasSkin = Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
		FStaticMeshData& Geometry = Result.Geometry;
		if (!Mesh->GetPolygonCount()) { Warn(Warnings, "Empty mesh: " + FString(Node->GetName())); return true; }

		// multiple layer에 대한 처리는 하지 않는다.
		FbxLayer* Layer = Mesh->GetLayer(0);
		if (!Layer || !Layer->GetNormals())
		{
			if (!Mesh->GenerateNormals(true, false)) { Error = "normal generation failed"; return false; }
			Layer = Mesh->GetLayer(0);
			Warn(Warnings, "Missing normals generated: " + FString(Node->GetName()));
		}
		if (!Layer || !Layer->GetNormals()) { Error = "no normal layer"; return false; }

		const auto* Normals = Layer->GetNormals();
		const auto* UVs = Layer->GetUVs();
		const auto* Colors = Layer->GetVertexColors();
		const auto* Materials = Layer->GetMaterials();
		if (!UVs) Warn(Warnings, "Missing UVs use (0,0): " + FString(Node->GetName()));
		if (Mesh->GetElementUVCount() > 1) Warn(Warnings, "Only the first layer-0 UV set is used: " + FString(Node->GetName()));
		if (Mesh->GetDeformerCount(FbxDeformer::eBlendShape)) Warn(Warnings, "Blend shapes are not imported: " + FString(Node->GetName()));
		if (Mesh->GetDeformerCount(FbxDeformer::eVertexCache)) Warn(Warnings, "Vertex cache deformation is not imported: " + FString(Node->GetName()));

		// Mesh 자체에 적용되는 Geometric Translation/Rotation/Scale을 G 행렬로 만든다.
		// 전달받은 현재 또는 Bind Global과 결합해 정점을 굽고, Normal에는 결합 행렬의 역전치를 적용한다.
		FbxAMatrix Geometric;
		Geometric.SetT(Node->GetGeometricTranslation(FbxNode::eSourcePivot));
		Geometric.SetR(Node->GetGeometricRotation(FbxNode::eSourcePivot));
		Geometric.SetS(Node->GetGeometricScaling(FbxNode::eSourcePivot));
		
		// SDK는 열벡터이므로 엔진의 p*G*M에 해당하는 행렬은 M*G다.
		const FbxAMatrix Total = Global * Geometric;
		// 좌표계 반사와 Polygon 순서는 DeepConvertScene에서 변환됐다.
		// 여기서는 정점에 굽는 Geometric/Global의 음수 Scale에 의한 뒤집기만 검사한다.
		const double FinalDeterminant = Total.Determinant();
		if (!std::isfinite(FinalDeterminant) || FinalDeterminant == 0) { Error = "singular/non-finite mesh transform"; return false; }
		
		const FbxAMatrix NormalMatrix = Total.Inverse().Transpose();
		
		ReadMaterials(Node, Folder, UVs ? UVs->GetName() : "", Geometry.MaterialSlots, Warnings);
		
		std::vector<TArray<uint32>> SlotIndices(Geometry.MaterialSlots.Num());
		TMap<FVertexKey, uint32, FVertexKeyHash> VerticesByValue;
		// 삼각형마다 Material Slot을 정하고, 세 Polygon Vertex를 엔진 정점 번호로 변환한다.
		// 정점 조합은 Node 전체에서 공유하고, 삼각형 Index는 Slot별로 모아 나중에 Section으로 묶는다.
		for (int Polygon = 0; Polygon < Mesh->GetPolygonCount(); ++Polygon)
		{
			if (Mesh->GetPolygonSize(Polygon) != 3) { Error = "triangulation left a non-triangle"; return false; }
			int Slot = 0;
			// ByPolygon이면 현재 Polygon 칸, AllSame이면 0번 칸을 읽는다.
			// Material IndexArray의 값은 Node의 Material Slot 번호다. 누락되거나 범위를 벗어나면 Slot 0을 사용한다.
			if (Materials)
			{
				const auto Mapping = Materials->GetMappingMode();
				if (Mapping != FbxLayerElement::eByPolygon && Mapping != FbxLayerElement::eAllSame) { Error = "unsupported material mapping"; return false; }
				if (Materials->GetReferenceMode() != FbxLayerElement::eIndexToDirect) { Error = "unsupported material reference mode"; return false; }
				const int Index = Mapping == FbxLayerElement::eAllSame ? 0 : Polygon;
				Slot = Index < Materials->GetIndexArray().GetCount() ? Materials->GetIndexArray().GetAt(Index) : -1;
				if (Slot < 0 || Slot >= Geometry.MaterialSlots.Num()) { Slot = 0; Warn(Warnings, "Invalid material assignment uses slot 0: " + FString(Node->GetName())); }
			}
			else Warn(Warnings, "Missing material layer uses slot 0: " + FString(Node->GetName()));
			uint32 Triangle[3];
			// 각 꼭짓점의 CP(위치 번호)와 PV(Polygon Vertex 칸 번호)로 Normal/UV/Color를 조회한다.
			// 엔진 좌표계의 Position/Normal을 옮기고 Normal 정규화와 UV의 V 반전을 적용한다.
			// 같은 (CP, Normal, UV, Color) 조합은 기존 정점을 재사용하고, 새 정점이면 CP 대응표와 AABB도 갱신한다.
			// Triangle에 넣는 값은 CP 번호가 아니라 최종 Geometry.Vertices의 정점 번호다.
			for (int Corner = 0; Corner < 3; ++Corner)
			{
				const int CP = Mesh->GetPolygonVertex(Polygon, Corner);
				if (CP < 0 || CP >= Mesh->GetControlPointsCount()) { Error = "control point index out of range"; return false; }
				const int PV = Mesh->GetPolygonVertexIndex(Polygon) + Corner;
				FbxVector4 Normal;
				FbxVector2 UV(0, 0);
				FbxColor Color(1, 1, 1, 1);
				if (!ReadElement(Normals, CP, PV, Polygon, Normal, Error)) return false;
				if (UVs && !ReadElement(UVs, CP, PV, Polygon, UV, Error)) return false;
				if (Colors && !ReadElement(Colors, CP, PV, Polygon, Color, Error)) return false;
				// 방향에는 3x3 성분만 곱하여 평행이동을 제외한다.
				FbxVector4 TransformedNormal(0, 0, 0, 0);
				for (int Row = 0; Row < 3; ++Row)
					for (int Column = 0; Column < 3; ++Column) TransformedNormal[Row] += NormalMatrix.Get(Column, Row) * Normal[Column];
				const double Length = std::sqrt(TransformedNormal[0] * TransformedNormal[0] + TransformedNormal[1] * TransformedNormal[1] + TransformedNormal[2] * TransformedNormal[2]);
				if (!std::isfinite(Length) || Length == 0) { Error = "zero/non-finite normal"; return false; }
				FVertex Vertex;
				Vertex.Position = ToVector(Total.MultT(Mesh->GetControlPointAt(CP)));
				Vertex.Normal = ToVector(TransformedNormal / Length);
				Vertex.UV = FVector2(static_cast<float>(UV[0]), UVs ? static_cast<float>(1.0 - UV[1]) : 0.0f);
				Vertex.Color = FVector4(static_cast<float>(Color.mRed), static_cast<float>(Color.mGreen), static_cast<float>(Color.mBlue), static_cast<float>(Color.mAlpha));
				const FVertexKey Key{ CP, { Vertex.Normal.X, Vertex.Normal.Y, Vertex.Normal.Z, Vertex.UV.X, Vertex.UV.Y, Vertex.Color.X, Vertex.Color.Y, Vertex.Color.Z, Vertex.Color.W } };
				if (!std::isfinite(Vertex.Position.X) || !std::isfinite(Vertex.Position.Y) || !std::isfinite(Vertex.Position.Z) || !std::all_of(Key.second.begin(), Key.second.end(), [](float V) { return std::isfinite(V); })) { Error = "non-finite vertex attribute"; return false; }
				if (const uint32* ExistingIndex = VerticesByValue.Find(Key))
				{
					Triangle[Corner] = *ExistingIndex;
				}
				else
				{
					const uint32 NewIndex = static_cast<uint32>(Geometry.Vertices.Num());
					VerticesByValue.Add(Key, NewIndex);
					if (Geometry.Vertices.IsEmpty()) Geometry.AABB = { Vertex.Position, Vertex.Position };
					else Geometry.AABB.Expand({ Vertex.Position, Vertex.Position });
					Geometry.Vertices.Add(Vertex);
					Result.ControlPointIndices.Add(CP);
					Triangle[Corner] = NewIndex;
				}
			}
			if (FinalDeterminant < 0) std::swap(Triangle[1], Triangle[2]);
			for (uint32 Index : Triangle) SlotIndices[Slot].Add(Index);
		}
		// Slot별로 모아 둔 Index를 최종 배열에 이어 붙여 같은 Material을 쓰는 삼각형이 연속하도록 한다.
		// 각 Section에는 이어 붙이기 전 시작 위치, Index 개수, Material Slot 번호를 기록한다. 사용하지 않은 Slot은 Section을 만들지 않는다.
		for (int Slot = 0; Slot < Geometry.MaterialSlots.Num(); ++Slot)
		{
			if (SlotIndices[Slot].IsEmpty()) continue;
			Geometry.Sections.Add({ static_cast<uint32>(Geometry.Indices.Num()), static_cast<uint32>(SlotIndices[Slot].Num()), static_cast<uint32>(Slot) });
			Geometry.Indices.Append(SlotIndices[Slot]);
		}
		return true;
	}
}

FFbxImporter::~FFbxImporter()
{
	Close();
}

bool FFbxImporter::Open(const FString& Path, FString& OutError, const FString& ExtractionFolder)
{
	Close();
	OutError.clear();

	Manager = FbxManager::Create();
	if (!Manager)
	{
		OutError = "FBX: failed to create SDK manager";
		return false;
	}

	FbxIOSettings* Settings = FbxIOSettings::Create(Manager, IOSROOT);	// IOSRoot라는 이름으로 FbxIOSettings 객체를 생성
	if (!Settings)
	{
		OutError = "FBX: failed to create IO settings";
		Close();
		return false;
	}
	Manager->SetIOSettings(Settings);


	// Asset Import는 호출자가 관리하는 임시 폴더로 추출한다. 폴더 없는 Scene 조회는 디스크에 쓰지 않는다.
	Settings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, !ExtractionFolder.empty());

	Scene = FbxScene::Create(Manager, "ImportScene");
	// importer에 이름을 지정하면, 이름을 기반으로 로그를 남길 때 유용하게 사용할 수 있지만
	// 현재는 multiple importer가 필요하지 않아서 이름은 지정하지 않았다.
	FbxImporter* Reader = FbxImporter::Create(Manager, "");
	if (!Scene || !Reader)
	{
		OutError = "FBX: failed to create scene or reader";
		Close();
		return false;
	}

	if (!Reader->Initialize(Path.c_str(), -1, Settings))
	{
		OutError = "FBX: cannot open '" + Path + "': " + Reader->GetStatus().GetErrorString();
		Close();
		return false;
	}
	if (!Reader->IsFBX())
	{
		OutError = "FBX: not an FBX file: '" + Path + "'";
		Close();
		return false;
	}
	FEmbeddedExtraction Extraction;
	FbxEmbeddedFileCallback* Callback = nullptr;
	if (!ExtractionFolder.empty())
	{
		Extraction.Folder = PathOf(ExtractionFolder);
		Reader->SetEmbeddingExtractionFolder(ExtractionFolder.c_str());
		Callback = FbxEmbeddedFileCallback::Create(Manager, "EmbeddedTextures");
		if (!Callback || Callback->RegisterReadFunction(FEmbeddedExtraction::Read, &Extraction) < 0)
		{
			OutError = "FBX: cannot create embedded texture callback";
			Close();
			return false;
		}
		// Initialize가 callback을 초기화하므로 Initialize 이후, Import 이전에 등록한다.
		Reader->SetEmbeddedFileReadCallback(Callback);
	}
	const bool Imported = Reader->Import(Scene);
	// callback의 지역 상태를 가리키는 포인터가 Open 반환 뒤에 남지 않도록 해제한다.
	if (Callback)
	{
		Reader->SetEmbeddedFileReadCallback(nullptr);
		Callback->Destroy();
	}
	if (!Imported || !Extraction.Error.empty())
	{
		OutError = Extraction.Error.empty() ? "FBX: cannot import '" + Path + "': " + Reader->GetStatus().GetErrorString() : Extraction.Error;
		Close();
		return false;
	}
	Extraction.ResolveTextures(Scene);

	// Creator는 Reader가 살아 있을 때 읽어 두고 Blender Armature 예외 판별에 사용한다.
	const FbxIOFileHeaderInfo* Header = Reader->GetFileHeaderInfo();
	bIsBlender = Header && FString(Header->mCreator.Buffer()).starts_with("Blender");
	// 파일 읽기는 끝났으므로 Reader를 닫고 메모리에 있는 Scene만 유지한다.
	Reader->Destroy();
	SourcePath = Path;
	return true;
}

void FFbxImporter::Close()
{
	ClearSkeletonData();
	MeshNodes.Empty();
	SourcePath.clear();
	bIsBlender = false;
	// Manager가 아직 남아 있는 Reader, Scene, IOSettings를 함께 해제한다.
	if (Manager)
		Manager->Destroy();
	Manager = nullptr;
	Scene = nullptr;
}

// 열린 Scene의 축/단위를 변환한 뒤, Mesh Node별 Geometry/Material과 Control Point 대응표를 만든다.
// 모든 Node 추출이 성공했을 때만 Results를 MeshNodes에 보관한다.
bool FFbxImporter::ReadMeshData(TArray<FString>& OutWarnings, FString& OutError)
{
	ClearSkeletonData();
	MeshNodes.Empty();
	OutWarnings.Empty();
	OutError.clear();
	if (!Scene) 
	{ 
		OutError = "FBX: no scene is open"; 
		return false; 
	}

	FbxRootNodeUtility::RemoveAllFbxRoots(Scene);
	// Mesh/Bone을 읽기 전에 장면 전체를 엔진 좌표계(+Z 위, +X 앞, 왼손)로 맞춘다.
	// DeepConvertScene은 Transform뿐 아니라 정점/Normal, Polygon 순서, Pose/Cluster도 변환한다.
	// 이후 값을 옮길 때 Y나 Quaternion을 다시 반전하지 않는다.
	const FbxAxisSystem Target(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	if (Scene->GetGlobalSettings().GetAxisSystem() != Target) Target.DeepConvertScene(Scene);
	// 단위도 설정값만 바꾸는 것이 아니라 Scene 데이터를 cm 기준으로 실제 변환한다.
	if (Scene->GetGlobalSettings().GetSystemUnit() != FbxSystemUnit::cm) FbxSystemUnit::cm.ConvertScene(Scene);
	// 축/단위 변환 전 자식 Node의 Global 캐시가 남지 않도록 비운다. UE ConvertScene 경로와 같은 처리다.
	Scene->GetAnimationEvaluator()->Reset();
	
	FbxGeometryConverter Converter(Manager);
	TArray<FFbxMeshNodeData> Results;
	// Scene의 Node 목록에서 Mesh를 찾는다. Mesh가 없는 Node는 건너뛴다.
	// 지원하지 않는 Geometry/복수 Mesh Attribute는 오류로 처리하고,
	// 각 Mesh를 삼각형화한 뒤 새 Mesh에서 Node별 데이터를 추출한다.
	for (int I = 0; I < Scene->GetNodeCount(); ++I)
	{
		FbxNode* Node = Scene->GetNode(I);
		int MeshCount = 0;
		for (int A = 0; A < Node->GetNodeAttributeCount(); ++A)
		{
			const auto Type = Node->GetNodeAttributeByIndex(A)->GetAttributeType();
			if (Type == FbxNodeAttribute::eMesh) 
				++MeshCount;
			if (Type == FbxNodeAttribute::eNurbs || Type == FbxNodeAttribute::eNurbsSurface || Type == FbxNodeAttribute::ePatch) 
			{ 
				OutError = "FBX: unsupported Patch/NURBS: " + FString(Node->GetName()); 
				return false; 
			}
		}
		if (!MeshCount) continue;
		if (MeshCount != 1 || !Node->GetMesh()) 
		{ 
			OutError = "FBX: unsupported multiple/non-default mesh attributes: " + FString(Node->GetName()); 
			return false; 
		}

		// replace를 true로 하였기때문에, 기존 데이터는 사라지고, 삼각형으로 변환된 데이터만 남게 된다.
		if (!Converter.Triangulate(Node->GetMesh(), true)) 
		{ 
			OutError = "FBX: triangulation failed: " + FString(Node->GetName()); 
			return false; 
		}
		// Triangulate가 기존 Mesh를 교체할 수 있으므로 Node에서 다시 얻는다.
		FFbxMeshNodeData Result;
		if (!ReadMeshNode(Node, Node->GetMesh(), Node->EvaluateGlobalTransform(), PathOf(SourcePath).parent_path(), Result, OutWarnings, OutError))
		{
			OutError = "FBX: " + FString(Node->GetName()) + ": " + OutError;
			return false;
		}
		Results.Add(std::move(Result));
	}
	if (Results.IsEmpty()) { OutError = "FBX: no mesh nodes"; return false; }
	MeshNodes = std::move(Results);
	return true;
}

// Node별로 추출한 Geometry를 하나의 Static 출력 데이터로 합친다.
// Skin이 있는 Node도 포함하며, 이미 구워진 Position/Normal에 Transform을 다시 적용하지 않는다.
void FFbxImporter::BuildStaticMesh(FStaticMeshData& OutData) const
{
	OutData = {};
	for (const FFbxMeshNodeData& Node : MeshNodes)
		AppendGeometry(Node.Geometry, OutData);
}


void FFbxImporter::ClearSkeletonData()
{
	Bones.Empty();
	BoneIndices.Empty();
	RigidBindMeshes.Empty();
	for (FFbxMeshNodeData& Mesh : MeshNodes) Mesh.RigidBoneIndex = -1;
}

// ReadMeshData로 축과 단위를 변환한 Scene에서 Bone 계층과 Bind를 읽는다. 실패 시 Skeleton 결과는 비워 둔다.
bool FFbxImporter::ReadSkeletonData(TArray<FString>& OutWarnings, FString& OutError)
{
	ClearSkeletonData();
	OutWarnings.Empty();
	OutError.clear();
	if (!Scene || MeshNodes.IsEmpty()) { OutError = "FBX: read mesh data before skeleton data"; return false; }

	// 여러 Mesh의 Cluster를 모아 Bone 계층과 Bind를 조회한다. Weight 값은 읽지 않는다.
	TArray<FbxCluster*> Clusters;
	FbxNode* Root = nullptr;
	for (const FFbxMeshNodeData& Mesh : MeshNodes)
	{
		if (!Mesh.bHasSkin) continue;
		FbxMesh* Geometry = Mesh.Node->GetMesh();
		for (int S = 0; S < Geometry->GetDeformerCount(FbxDeformer::eSkin); ++S)
		{
			auto* Skin = static_cast<FbxSkin*>(Geometry->GetDeformer(S, FbxDeformer::eSkin));
			if (!Skin->GetClusterCount()) { OutError = "FBX: Skin has no clusters: " + FString(Mesh.Node->GetName()); return false; }
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				FbxNode* Link = Cluster->GetLink();
				if (!Link) { OutError = "FBX: Cluster has no Link: " + FString(Mesh.Node->GetName()); return false; }
				FbxNode* Candidate = FindSkeletonRoot(Link, Scene, bIsBlender);
				if (Root && Root != Candidate) { OutError = "FBX: multiple skeleton roots"; return false; }
				Root = Candidate;
				Clusters.Add(Cluster);
			}
		}
	}
	if (!Root) { OutError = "FBX: no skeleton bones"; return false; }
	TArray<FbxNode*> Nodes;
	CollectBoneNodes(Root, Nodes);
	if (Nodes.IsEmpty()) { OutError = "FBX: no skeleton bones"; return false; }
	// 기존 FSkinWeight::BoneIndex가 uint8이므로 최종 출력으로 표현할 수 있는 Bone 수에 맞춘다.
	if (Nodes.Num() > 256) { OutError = "FBX: bone count exceeds 256"; return false; }

	TMap<FbxNode*, int32> ResultIndices;
	TSet<FString> Names;
	TArray<FBoneInfo> ResultBones;
	TArray<FbxAMatrix> BindGlobals;
	TArray<FMatrix> BindWorlds;
	for (int32 I = 0; I < Nodes.Num(); ++I)
	{
		FbxNode* Node = Nodes[I];
		FBoneInfo Bone;
		Bone.Name = Node->GetName();
		if (Names.Contains(Bone.Name)) { OutError = "FBX: duplicate bone name: " + Bone.Name; return false; }
		Names.Add(Bone.Name);
		if (I > 0)
		{
			const int32* Parent = ResultIndices.Find(Node->GetParent());
			if (!Parent) { OutError = "FBX: bone parent is not in skeleton: " + Bone.Name; return false; }
			Bone.ParentIndex = *Parent;
		}

		const FbxAMatrix Global = ReadBindGlobal(Scene, Node, Clusters, OutWarnings);
		const double GlobalDeterminant = Global.Determinant();
		if (!IsFiniteMatrix(Global) || !std::isfinite(GlobalDeterminant) || GlobalDeterminant == 0) { OutError = "FBX: invalid Bind Global: " + Bone.Name; return false; }
		const FbxAMatrix Local = Bone.ParentIndex < 0 ? Global : BindGlobals[Bone.ParentIndex].Inverse() * Global;
		if (!IsFiniteMatrix(Local)) { OutError = "FBX: non-finite Bind Local: " + Bone.Name; return false; }
		const FbxVector4 Translation = Local.GetT();
		const FbxQuaternion Rotation = Local.GetQ();
		const FbxVector4 Scale = Local.GetS();
		// Scene의 Bind 행렬도 이미 엔진 좌표계이므로 분해한 TRS에 추가 반사를 하지 않는다.
		Bone.LocalBindPose.Location = ToVector(Translation);
		Bone.LocalBindPose.Scale = FVector(static_cast<float>(Scale[0]), static_cast<float>(Scale[1]), static_cast<float>(Scale[2]));
		// SDK에서 Euler로 변환하여 정확히 +/-90도인 회전도 보존한다. FRotator 저장 순서는 Pitch(Y), Yaw(Z), Roll(X)다.
		FbxAMatrix EngineRotation;
		EngineRotation.SetQ(Rotation);
		const FbxVector4 Euler = EngineRotation.GetR();
		Bone.LocalBindPose.Rotation = FRotator(static_cast<float>(Euler[1]), static_cast<float>(Euler[2]), static_cast<float>(Euler[0])).Quaternion();
		const FMatrix LocalMatrix = Bone.LocalBindPose.GetLocalMatrix();
		const double LocalDeterminant = LocalMatrix.Determinant();
		if (!std::isfinite(LocalDeterminant) || LocalDeterminant == 0) { OutError = "FBX: non-finite/zero-scale Bind Local: " + Bone.Name; return false; }
		// 런타임과 같은 저장 Local 누적에서 역행렬을 만들어 TRS 분해 후의 자세와 일치시킨다.
		const FMatrix World = Bone.ParentIndex < 0 ? LocalMatrix : LocalMatrix * BindWorlds[Bone.ParentIndex];
		Bone.InverseBindPose = World.Inverse();
		for (int R = 0; R < 4; ++R)
		{
			for (int C = 0; C < 4; ++C)
			{
				if (!std::isfinite(World.M[R][C]) || !std::isfinite(Bone.InverseBindPose.M[R][C])) { OutError = "FBX: non-finite accumulated Bind: " + Bone.Name; return false; }
			}
		}
		ResultIndices.Add(Node, I);
		ResultBones.Add(std::move(Bone));
		BindGlobals.Add(Global);
		BindWorlds.Add(World);
	}
	for (FbxCluster* Cluster : Clusters)
	{
		if (!ResultIndices.Contains(Cluster->GetLink())) { OutError = "FBX: Cluster Link is outside the selected bone hierarchy"; return false; }
	}

	// 현재 Global 기준 자료는 Static 출력에 남기고, rigid만 선택된 Bind Global로 다시 읽는다.
	// 모든 추출이 성공하기 전에는 멤버를 갱신하지 않아 실패 후 부분 결과가 남지 않는다.
	TMap<FbxNode*, FFbxMeshNodeData> ResultRigidMeshes;
	constexpr double RigidPoseTolerance = 1e-6;
	for (const FFbxMeshNodeData& Mesh : MeshNodes)
	{
		if (Mesh.bHasSkin) continue;
		const int32* BoneIndex = ResultIndices.Find(Mesh.Node);
		if (!BoneIndex) continue;
		const FbxAMatrix& Bind = BindGlobals[*BoneIndex];
		const FbxAMatrix Current = Mesh.Node->EvaluateGlobalTransform();
		double MaxDifference = 0;
		for (int Row = 0; Row < 4; ++Row)
			for (int Column = 0; Column < 4; ++Column) MaxDifference = std::max(MaxDifference, std::abs(Current.Get(Row, Column) - Bind.Get(Row, Column)));
		if (MaxDifference > RigidPoseTolerance) Warn(OutWarnings, "FBX: rigid mesh baked at bind pose (differs from current pose): " + FString(Mesh.Node->GetName()));
		FFbxMeshNodeData Result;
		if (!ReadMeshNode(Mesh.Node, Mesh.Node->GetMesh(), Bind, PathOf(SourcePath).parent_path(), Result, OutWarnings, OutError))
		{
			OutError = "FBX: " + FString(Mesh.Node->GetName()) + ": " + OutError;
			return false;
		}
		Result.RigidBoneIndex = *BoneIndex;
		ResultRigidMeshes.Add(Mesh.Node, std::move(Result));
	}
	Bones = std::move(ResultBones);
	BoneIndices = std::move(ResultIndices);
	RigidBindMeshes = std::move(ResultRigidMeshes);
	for (FFbxMeshNodeData& Mesh : MeshNodes)
		if (const FFbxMeshNodeData* Rigid = RigidBindMeshes.Find(Mesh.Node)) Mesh.RigidBoneIndex = Rigid->RigidBoneIndex;
	return true;
}

void FFbxImporter::ImportFromFile()
{
	// TODO: FBX 파일을 읽어 FbxScene을 준비한다.
	// TODO: 임포트 옵션에 맞춰 좌표계와 단위를 변환한다.
	// TODO: 임포트 대상 Mesh와 Skeleton Node를 수집한다.
}

void FFbxImporter::ImportStaticMeshAsSingle()
{
	// TODO: 정점, 인덱스, 머티리얼 데이터를 추출한다.
	// TODO: UStaticMesh 에셋을 구성해 반환한다.
}

void FFbxImporter::ImportSkeletalMesh()
{
	// TODO: 본 계층과 Bind Pose를 추출해 대상 스켈레톤을 구성한다.
	// TODO: 정점, 인덱스, Skin Weight, 머티리얼로 USkeletalMesh를 구성한다.
	// TODO: 애니메이션 임포트 옵션이 켜져 있으면 ImportAnimations를 호출한다.
	// TODO: 생성된 메시와 스켈레톤, 시퀀스들을 반환한다.
}

bool FFbxImporter::ImportAnimations(TArray<UAnimSequence*>& OutSequences, TArray<FString>& OutWarnings,
	FString& OutError, const FFbxAnimationImportOptions& Options)
{
	OutWarnings.Empty();
	OutError.clear();
	if (!Scene || Bones.IsEmpty() || BoneIndices.Num() != Bones.Num())
	{
		OutError = "FBX: read skeleton data before importing animations";
		return false;
	}

	if (Options.bResample && Options.ResampleRate < 0)
	{
		OutError = "FBX: animation resample rate must be zero (automatic) or positive";
		return false;
	}

	int32 ResampleRate = DefaultAnimationSampleRate;
	if (Options.bResample)
	{
		ResampleRate = Options.ResampleRate > 0 ? Options.ResampleRate : GetMaxSampleRate();
	}
	const FFrameRate SampleRate(ResampleRate, 1);

	TArray<std::unique_ptr<UAnimSequence>> Sequences;
	const int32 AnimStackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
	for (int32 StackIndex = 0; StackIndex < AnimStackCount; ++StackIndex)
	{
		FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		if (!AnimStack) continue;

		// 1) 시간 범위 (Unreal Exported Time)
		FbxTimeSpan TimeSpan = GetAnimationTimeSpan(*AnimStack);

		// 2) 검증 + 프레임 경계 정렬. 정렬되면 TimeSpan이 수정된다.
		const FbxTimeSpan OriginalSpan = TimeSpan;
		FString StackError;
		if (!ValidateAnimStack(*AnimStack, SampleRate, TimeSpan, StackError))
		{
			OutWarnings.Add("FBX: skipped animation stack " + FString(AnimStack->GetName()) + ": " + StackError);
			continue;
		}
		if (TimeSpan.GetStart() != OriginalSpan.GetStart() || TimeSpan.GetStop() != OriginalSpan.GetStop())
		{
			OutWarnings.Add("FBX: animation stack " + FString(AnimStack->GetName())
				+ ": time range snapped to " + std::to_string(ResampleRate) + "fps frame boundaries");
		}

		// 3) 시퀀스 생성. PlayLength는 ImportAnimation이 키 개수로 확정하므로 여기서는 0으로 둔다.
		std::unique_ptr<UAnimSequence> Sequence(NewObject<UAnimSequence>());
		Sequence->Initialize(AnimStack->GetName(), {}, SampleRate, 0.0f);

		// 4) 데이터 채우기. 유효한 본 트랙이 하나도 없으면 저장할 수 없으므로 건너뛴다.
		ImportAnimation(*Sequence, *AnimStack, SampleRate, TimeSpan, OutWarnings);
		if (Sequence->GetDataModel()->GetBoneAnimationTracks().IsEmpty())
		{
			OutWarnings.Add("FBX: skipped animation stack " + FString(AnimStack->GetName()) + ": no valid bone tracks");
			continue;
		}
		Sequences.Add(std::move(Sequence));
	}

	if (Sequences.IsEmpty())
	{
		OutError = "FBX: no valid animation stacks";
		return false;
	}

	OutSequences.Reserve(OutSequences.Num() + Sequences.Num());
	for (auto& Sequence : Sequences)
	{
		OutSequences.Add(Sequence.release());
	}
	return true;
}

int32 FFbxImporter::GetMaxSampleRate() const
{
	TSet<int32> CurveRates;
	const int32 StackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
	for (int32 StackIndex = 0; StackIndex < StackCount; ++StackIndex)
	{
		FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		if (!Stack) continue;
		const int32 LayerCount = Stack->GetMemberCount<FbxAnimLayer>();
		for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
		{
			FbxAnimLayer* Layer = Stack->GetMember<FbxAnimLayer>(LayerIndex);
			if (!Layer) continue;
			for (const auto& [Node, BoneIndex] : BoneIndices)
			{
				GetNodeSampleRate(Node, Layer, CurveRates);
			}
		}
	}

	if (CurveRates.IsEmpty()) return DefaultAnimationSampleRate;
	int32 SampleRate = 1;
	for (int32 CurveRate : CurveRates)
	{
		if (CurveRate >= MaxAnimationReferenceRate)
		{
			SampleRate = std::max(SampleRate, CurveRate);
		}
		else if (SampleRate < MaxAnimationReferenceRate)
		{
			SampleRate = static_cast<int32>(std::min<int64>(
				std::lcm<int64>(SampleRate, CurveRate), MaxAnimationReferenceRate));
		}
	}
	return SampleRate;
}

FbxTimeSpan FFbxImporter::GetAnimationTimeSpan(const FbxAnimStack& AnimStack) const
{
	// Unreal의 Exported Time 경로에 해당한다.
	return AnimStack.GetLocalTimeSpan();
}

bool FFbxImporter::ValidateAnimStack(const FbxAnimStack& AnimStack, FFrameRate SampleRate, FbxTimeSpan& InOutTimeSpan, FString& OutError) const
{
	OutError.clear();
	if (AnimStack.GetMemberCount<FbxAnimLayer>() == 0)
	{
		OutError = "animation stack has no layers";
		return false;
	}
	if (InOutTimeSpan.GetStart() <= FBXSDK_TIME_MINUS_INFINITE || InOutTimeSpan.GetStop() >= FBXSDK_TIME_INFINITE
		|| InOutTimeSpan.GetStop() <= InOutTimeSpan.GetStart())
	{
		OutError = "animation time range must be finite and have positive duration";
		return false;
	}

	// Unreal bSnapToClosestFrameBoundary: 길이가 프레임 정수배가 아니면 Start/Stop을 가까운 프레임으로 맞춘다.
	// 정렬해 두면 샘플링 시 마지막 프레임(Stop)이 부동소수 오차로 빠지지 않는다.
	constexpr float SubFrameTolerance = 1.0e-4f;
	const auto SnapToFrame = [&SampleRate](const FbxTime& Time)
	{
		const FFrameTime FrameTime = SampleRate.AsFrameTime(Time.GetSecondDouble());
		if (FrameTime.SubFrame <= SubFrameTolerance || FrameTime.SubFrame >= 1.0f - SubFrameTolerance)
		{
			return Time; // 이미 프레임 경계 위
		}
		FbxTime Snapped;
		Snapped.SetSecondDouble(SampleRate.AsSeconds(FFrameTime(FrameTime.RoundToFrame())));
		return Snapped;
	};
	InOutTimeSpan.SetStart(SnapToFrame(InOutTimeSpan.GetStart()));
	InOutTimeSpan.SetStop(SnapToFrame(InOutTimeSpan.GetStop()));

	if (InOutTimeSpan.GetStop() <= InOutTimeSpan.GetStart())
	{
		OutError = "animation is shorter than one frame at the sample rate";
		return false;
	}
	return true;
}

void FFbxImporter::ImportAnimation(UAnimSequence& Sequence, FbxAnimStack& AnimStack,
	FFrameRate SampleRate, const FbxTimeSpan& TimeSpan, TArray<FString>& OutWarnings)
{
	// 1) 프레임 수 결정. TimeSpan은 ValidateAnimStack에서 프레임 경계로 맞춰져 있으므로 반올림하면 정확한 정수다.
	//    포즈 하나뿐인 클립도 재생할 수 있도록 최소 1프레임(키 2개)을 보장한다. (Unreal SetNumberOfFrames(Max(N, 1)))
	const double DurationSeconds = TimeSpan.GetDuration().GetSecondDouble();
	const int32 NumberOfFrames = std::max(1, static_cast<int32>(std::lround(DurationSeconds * SampleRate.AsDecimal())));
	const int32 NumberOfKeys = NumberOfFrames + 1;
	const float PlayLength = static_cast<float>(NumberOfFrames * SampleRate.AsInterval());

	// 2) 샘플 시각 목록. Start + i 프레임을 매번 새로 계산해 누적 오차 없이 마지막 키가 정확히 Stop에 놓이게 한다.
	TArray<FbxTime> SampleTimes;
	SampleTimes.Reserve(NumberOfKeys);
	const double StartSeconds = TimeSpan.GetStart().GetSecondDouble();
	for (int32 KeyIndex = 0; KeyIndex < NumberOfKeys; ++KeyIndex)
	{
		FbxTime SampleTime;
		SampleTime.SetSecondDouble(StartSeconds + SampleRate.AsSeconds(FFrameTime(FFrameNumber(KeyIndex))));
		SampleTimes.Add(SampleTime);
	}

	// 3) 가져올 스택을 활성화하고 본 트랙 추출. 끝나면 이전 활성 스택을 복원한다.
	FbxAnimStack* PreviousStack = Scene->GetCurrentAnimationStack();
	Scene->SetCurrentAnimationStack(&AnimStack);

	TArray<FBoneAnimationTrack> Tracks;
	ImportBoneTracks(SampleTimes, Tracks, OutWarnings);

	Scene->SetCurrentAnimationStack(PreviousStack);

	// 4) 모델 반영. 기존 데이터는 통째로 교체된다. (Unreal RemoveAllBoneTracks → SetFrameRate → SetNumberOfFrames)
	Sequence.Initialize(Sequence.GetClipName(), std::move(Tracks), SampleRate, PlayLength);
}

void FFbxImporter::ImportBoneTracks(const TArray<FbxTime>& SampleTimes, TArray<FBoneAnimationTrack>& OutTracks, TArray<FString>& OutWarnings)
{
	OutTracks.Empty();
	const int32 BoneCount = Bones.Num();
	const int32 KeyCount = SampleTimes.Num();
	if (BoneCount == 0 || KeyCount == 0) return;

	// 1) 스켈레톤 순서(본 인덱스)로 FBX 노드를 정렬한다. (Unreal SortedLinks + FindBoneIndex)
	TArray<FbxNode*> Nodes;
	Nodes.SetNum(BoneCount);
	for (const auto& [Node, BoneIndex] : BoneIndices)
	{
		Nodes[BoneIndex] = Node;
	}

	// 2) 본마다 트랙을 준비한다. 트랙 이름은 스켈레톤 본 이름이다.
	TArray<FBoneAnimationTrack> Tracks;
	Tracks.SetNum(BoneCount);
	TArray<uint8> bTrackValid;
	bTrackValid.SetNum(BoneCount);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		Tracks[BoneIndex].Name = Bones[BoneIndex].Name;
		auto& Keys = Tracks[BoneIndex].InternalTrack;
		Keys.PosKeys.Reserve(KeyCount);
		Keys.RotKeys.Reserve(KeyCount);
		Keys.ScaleKeys.Reserve(KeyCount);
		bTrackValid[BoneIndex] = 1;
	}

	// 3) 샘플 시각마다 모든 본의 Global을 평가한 뒤 Local = Parent⁻¹ × Global로 TRS 키를 추가한다.
	//    Global을 먼저 다 구해 두면 부모 Global을 다시 평가하지 않아도 된다.
	TArray<FbxAMatrix> Globals;
	Globals.SetNum(BoneCount);
	for (const FbxTime& Time : SampleTimes)
	{
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			Globals[BoneIndex] = Nodes[BoneIndex]->EvaluateGlobalTransform(Time);
		}

		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			if (!bTrackValid[BoneIndex]) continue;

			const int32 Parent = Bones[BoneIndex].ParentIndex;
			const FbxAMatrix Local = Parent < 0
				? Globals[BoneIndex]
				: Globals[Parent].Inverse() * Globals[BoneIndex];

			// Unreal ContainsNaN 검사: 0 스케일 부모의 역행렬 등으로 값이 깨지면 이 본의 트랙 전체를 버린다.
			if (!IsFiniteMatrix(Globals[BoneIndex]) || !IsFiniteMatrix(Local))
			{
				bTrackValid[BoneIndex] = 0;
				OutWarnings.Add("FBX: animation track " + Bones[BoneIndex].Name
					+ " has invalid transform (NaN or zero scale); the bone will use its bind pose");
				continue;
			}

			// 회전 변환은 ReadSkeletonData의 바인드 포즈와 같은 경로(Q → Euler → FRotator)를 써서
			// 바인드 포즈와 애니메이션 키의 축 규약을 일치시킨다. FRotator 순서는 Pitch(Y), Yaw(Z), Roll(X)다.
			FbxAMatrix RotationMatrix;
			RotationMatrix.SetQ(Local.GetQ());
			const FbxVector4 Euler = RotationMatrix.GetR();
			const FQuat Rotation = FRotator(
				static_cast<float>(Euler[1]),
				static_cast<float>(Euler[2]),
				static_cast<float>(Euler[0])).Quaternion();

			auto& Keys = Tracks[BoneIndex].InternalTrack;
			Keys.PosKeys.Add(ToVector(Local.GetT()));
			Keys.RotKeys.Add(Rotation);
			Keys.ScaleKeys.Add(ToVector(Local.GetS()));
		}
	}

	// 4) 유효한 트랙만 내보낸다. 모든 트랙의 키 개수는 KeyCount로 같다.
	OutTracks.Reserve(BoneCount);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		if (bTrackValid[BoneIndex])
		{
			OutTracks.Add(std::move(Tracks[BoneIndex]));
		}
	}
}

// Node별 Geometry와 Weight를 같은 정점 순서로 합친 뒤, SDK 포인터 없는 엔진 자료를 반환한다.
bool FFbxImporter::BuildSkeletalMesh(FSkeletalMeshData& OutData, TArray<FString>& OutWarnings, FString& OutError) const
{
	OutData = {};
	OutWarnings.Empty();
	OutError.clear();
	if (!Scene || Bones.IsEmpty()) { OutError = "FBX: read skeleton data before building skeletal mesh"; return false; }

	FStaticMeshData Geometry;
	FSkeletalMeshData Result;
	for (const FFbxMeshNodeData& SourceMesh : MeshNodes)
	{
		const FFbxMeshNodeData* Rigid = RigidBindMeshes.Find(SourceMesh.Node);
		const FFbxMeshNodeData& Mesh = Rigid ? *Rigid : SourceMesh;
		if (!Mesh.bHasSkin && Mesh.RigidBoneIndex < 0) continue;
		if (Mesh.bHasSkin)
		{
			TArray<FSkinWeight> ControlPointWeights;
			if (!ReadControlPointWeights(Mesh.Node, BoneIndices, ControlPointWeights, OutWarnings, OutError)) return false;
			// UV/Normal 등으로 분리된 Render Vertex에도 원본 Control Point와 같은 Weight를 전달한다.
			for (int32 CP : Mesh.ControlPointIndices) Result.SkinWeight.Add(ControlPointWeights[CP]);
		}
		else
		{
			FSkinWeight Weight;
			Weight.BoneIndex[0] = static_cast<uint8>(Mesh.RigidBoneIndex);
			Weight.Weight[0] = 1.0f;
			for (int32 I = 0; I < Mesh.Geometry.Vertices.Num(); ++I) Result.SkinWeight.Add(Weight);
		}
		AppendGeometry(Mesh.Geometry, Geometry);
	}
	// Position/Normal/UV는 이미 변환된 상태이므로 기존 Tangent 계산만 적용한다.
	Geometry.BuildTangentSpace();
	Result.BindVertices = std::move(Geometry.Vertices);
	Result.Indices = std::move(Geometry.Indices);
	Result.Sections.Reserve(Geometry.Sections.Num());
	for (const FStaticMeshSection& Section : Geometry.Sections)
		Result.Sections.Add({ Section.StartIndex, Section.IndexCount, Section.MaterialSlotIndex });
	Result.MaterialSlots.Reserve(Geometry.MaterialSlots.Num());
	for (FStaticMaterialSlot& Slot : Geometry.MaterialSlots)
		Result.MaterialSlots.Add({ std::move(Slot.Name), Slot.BaseColor, std::move(Slot.DiffuseTexturePath), std::move(Slot.NormalTexturePath) });
	Result.BindAABB = Geometry.AABB;
	Result.Bones = Bones;
	Result.SourcePath = SourcePath;
	if (!Result.Validate(OutError)) return false;
	OutData = std::move(Result);
	return true;
}

namespace
{
	FString Utf8Path(const std::filesystem::path& Path)
	{
		const auto Text = Path.generic_u8string();
		return FString(Text.begin(), Text.end());
	}

	// 영문·숫자·'-'·'_'만 남기고 나머지는 '_' 하나로 바꾼다. 비면 Fallback, Windows 예약어면 앞에 '_'를 붙인다.
	FString SanitizeFileName(const FString& Text, const FString& Fallback)
	{
		FString Name;
		for (char C : Text)
		{
			if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_') Name += C;
			else if (Name.empty() || Name.back() != '_') Name += '_';
		}
		if (Name.empty() || Name == "_") Name = Fallback;
		FString Upper = Name;
		for (char& C : Upper) if (C >= 'a' && C <= 'z') C -= 'a' - 'A';
		if (Upper == "CON" || Upper == "PRN" || Upper == "AUX" || Upper == "NUL" || (Upper.size() == 4 && (Upper.starts_with("COM") || Upper.starts_with("LPT")) && Upper[3] >= '1' && Upper[3] <= '9')) Name = "_" + Name;
		return Name;
	}

	FString AssetFileName(const std::filesystem::path& Path)
	{
		return SanitizeFileName(Utf8Path(Path.stem()), "FBX");
	}

	FString ToLowerAscii(FString Text)
	{
		for (char& C : Text) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
		return Text;
	}

	// 이 FBX가 저장한 애니메이션 파일: Name.anim.bin(스택 1개) 또는 Name_*.anim.bin(스택 여러 개)
	TArray<std::filesystem::path> FindAnimationFiles(const std::filesystem::path& Folder, const FString& Name)
	{
		TArray<std::filesystem::path> Files;
		std::error_code EC;
		if (!std::filesystem::is_directory(Folder, EC)) return Files;
		const FString Single = ToLowerAscii(Name + ".anim.bin");
		const FString Prefix = ToLowerAscii(Name + "_");
		for (const auto& Entry : std::filesystem::directory_iterator(Folder, EC))
		{
			if (!Entry.is_regular_file(EC)) continue;
			const FString File = ToLowerAscii(Utf8Path(Entry.path().filename()));
			if (File == Single || (File.starts_with(Prefix) && File.ends_with(".anim.bin"))) Files.Add(Entry.path());
		}
		return Files;
	}

	// 테스트용: 저장한 파일을 다시 읽어 메모리의 시퀀스와 같은지 확인한다.
	// bInOutLogSamples가 true면 회전이 변하는 첫 본의 키 값을 남기고 false로 바꾼다 (전체에서 한 번만 출력).
	bool VerifySavedAnimation(const std::filesystem::path& File, const UAnimSequence& Expected, const TArray<FBoneInfo>& Bones, bool& bInOutLogSamples)
	{
		const std::unique_ptr<UAnimSequence> LoadedSequence(NewObject<UAnimSequence>());
		FString ReadError;
		if (!AnimSequenceFile::Read(File, *LoadedSequence, ReadError))
		{
			HTR_LOG(Error, "FBX animation test: cannot read {}: {}", Expected.GetClipName(), ReadError);
			return false;
		}
		const UAnimDataModel* ExpectedModel = Expected.GetDataModel();
		const UAnimDataModel* DataModel = LoadedSequence->GetDataModel();
		const auto& Tracks = DataModel->GetBoneAnimationTracks();
		const FFrameRate SampleRate = ExpectedModel->GetFrameRate();
		const int32 SampleCount = ExpectedModel->GetNumberOfKeys();
		const float ExpectedLength = static_cast<float>((SampleCount - 1) * SampleRate.AsInterval());
		bool bPassed = SampleCount >= 2 && Tracks.Num() == Bones.Num()
			&& DataModel->GetNumberOfKeys() == SampleCount
			&& DataModel->GetNumberOfFrames() == SampleCount - 1
			&& DataModel->GetFrameRate() == SampleRate
			&& std::abs(DataModel->GetPlayLength() - ExpectedLength) <= 1.0e-4f
			&& LoadedSequence->GetClipName() == Expected.GetClipName();
		HTR_LOG(Info, "FBX animation test: stack={}, file={}, samples={}, tracks={}, bones={}, rate={}, length={}",
			LoadedSequence->GetClipName(), Utf8Path(File.filename()), SampleCount, Tracks.Num(), Bones.Num(),
			DataModel->GetFrameRate().AsDecimal(), DataModel->GetPlayLength());

		for (int32 BoneIndex = 0; BoneIndex < Tracks.Num(); ++BoneIndex)
		{
			const auto& Track = Tracks[BoneIndex];
			const auto& Keys = Track.InternalTrack;
			if (BoneIndex >= Bones.Num() || Track.Name != Bones[BoneIndex].Name
				|| Keys.PosKeys.Num() != SampleCount || Keys.RotKeys.Num() != SampleCount
				|| Keys.ScaleKeys.Num() != SampleCount)
			{
				bPassed = false;
				HTR_LOG(Error, "FBX animation test: invalid track {} (pos={}, rot={}, scale={})",
					Track.Name, Keys.PosKeys.Num(), Keys.RotKeys.Num(), Keys.ScaleKeys.Num());
			}
		}
		if (!bPassed || !bInOutLogSamples) return bPassed;

		for (const auto& Track : Tracks)
		{
			const auto& Keys = Track.InternalTrack;
			const FQuat& First = Keys.RotKeys[0];
			const bool bRotationChanges = std::any_of(Keys.RotKeys.begin(), Keys.RotKeys.end(), [&First](const FQuat& Key)
			{
				const float Dot = First.X * Key.X + First.Y * Key.Y + First.Z * Key.Z + First.W * Key.W;
				return std::abs(Dot) < 0.9999f;
			});
			if (!bRotationChanges) continue;

			for (const int32 KeyIndex : { 0, SampleCount / 2, SampleCount - 1 })
			{
				const FVector& Pos = Keys.PosKeys[KeyIndex];
				const FQuat& Rot = Keys.RotKeys[KeyIndex];
				const FVector& Scale = Keys.ScaleKeys[KeyIndex];
				HTR_LOG(Info, "FBX animation test: bone={}, key={}, pos=({}, {}, {}), rot=({}, {}, {}, {}), scale=({}, {}, {})",
					Track.Name, KeyIndex, Pos.X, Pos.Y, Pos.Z, Rot.X, Rot.Y, Rot.Z, Rot.W, Scale.X, Scale.Y, Scale.Z);
			}
			bInOutLogSamples = false;
			break;
		}
		return true;
	}

	bool SameSource(const FFbxSourceInfo& A, const FFbxSourceInfo& B)
	{
		return A.Path == B.Path && A.Size == B.Size && A.ModifiedTime == B.ModifiedTime && A.Mode == B.Mode;
	}

	// 새로 만든 작업 폴더만 정리한다. 복구에 실패하면 백업을 보존한다.
	struct FImportFiles
	{
		std::filesystem::path Folder;
		TArray<std::pair<std::filesystem::path, std::filesystem::path>> Files;
		bool bKeepBackup = false;
		~FImportFiles()
		{
			if (!Folder.empty() && !bKeepBackup) { std::error_code EC; std::filesystem::remove_all(Folder, EC); }
		}
		bool Commit(FString& Error)
		{
			// 모든 기존 파일을 먼저 보관한 뒤 개별 교체한다. 여러 파일 전체의 원자성은 보장하지 않는다.
			TArray<uint8> Existing;
			for (int32 I = 0; I < Files.Num(); ++I)
			{
				const auto& Target = Files[I].second;
				const bool Exists = std::filesystem::exists(Target);
				Existing.Add(Exists);
				if (Exists) std::filesystem::copy_file(Target, Folder / ("backup_" + std::to_string(I)));
			}
			for (int32 I = 0; I < Files.Num(); ++I)
			{
				const auto& [Temp, Target] = Files[I];
				if (Temp.empty())
				{
					if (!Existing[I] || DeleteFileW(Target.c_str())) continue;
				}
				else if (MoveFileExW(Temp.c_str(), Target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) continue;
				Error = "FBX: cannot replace file: " + Utf8Path(Files[I].second) + " (Windows " + std::to_string(GetLastError()) + ")";
				for (int32 J = I - 1; J >= 0; --J)
				{
					if (Existing[J])
					{
						const auto Backup = Folder / ("backup_" + std::to_string(J));
						if (!MoveFileExW(Backup.c_str(), Files[J].second.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) bKeepBackup = true;
					}
					else { std::error_code EC; std::filesystem::remove(Files[J].second, EC); if (EC) bKeepBackup = true; }
				}
				if (bKeepBackup) Error += "; rollback incomplete; backups kept at " + Utf8Path(Folder);
				return false;
			}
			return true;
		}
	};

	bool RegisterImported(const TArray<FString>& Paths, bool bChanged, TArray<FString>& Warnings, FString& Error)
	{
		for (const auto& Path : Paths)
		{
			const FString Key = std::filesystem::relative(PathOf(Path), "Assets").generic_string();
			if (bChanged && UAssetManager::FindLoadedAsset(Key))
			{
				// TODO: 현재는 재Import로 파일을 갱신해도 이미 로드된 Asset은 유지되어 변경 내용을 사용하려면 엔진을 재시작해야 한다.
				// Asset을 참조하는 Component와 렌더 자원의 수명을 고려해 Mesh/Material/Texture를 안전하게 갱신해야 한다.
				Warnings.Add("FBX: asset already loaded; restart the engine to use updated files: " + Key);
			}
			if (!UAssetManager::LoadFbxMesh(Path, Error))
			{
				Error = "FBX files are available, but asset registration failed: " + Error;
				return false;
			}
		}
		return true;
	}
}

bool FFbxImporter::ImportToAssets(const FString& Path, EFbxImportMode Mode, FFbxImportResult& OutResult, FString& OutError)
{
	OutResult = {};
	OutError.clear();
	struct FWarningLog
	{
		const TArray<FString>& Warnings;
		~FWarningLog() { for (const auto& Warning : Warnings) HTR_LOG(Warning, "{}", Warning); }
	} WarningLog{ OutResult.Warnings };
	if (Mode != EFbxImportMode::Static && Mode != EFbxImportMode::Skeletal) { OutError = "FBX: invalid Import mode"; return false; }
	try
	{
		const auto SourcePath = std::filesystem::canonical(PathOf(Path));
		FFbxSourceInfo Source;
		Source.Path = Utf8Path(SourcePath);
		Source.Size = std::filesystem::file_size(SourcePath);
		Source.ModifiedTime = std::filesystem::last_write_time(SourcePath).time_since_epoch().count();
		Source.Mode = Mode;
		FString Name = AssetFileName(SourcePath);
		const FString Stem = Utf8Path(SourcePath.stem());
		bool bTestAnimationImport = false;
#if defined(ENGINE_DEBUG)
		bTestAnimationImport = Mode == EFbxImportMode::Skeletal && Stem == "UAL1_Standard";
#endif
		if (std::none_of(Stem.begin(), Stem.end(), [](char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9');
		}))
		{
			// 영숫자가 남지 않는 출력 이름은 확장자를 제외한 원본 이름의 FNV-1a 32bit로 구분한다.
			// stem의 UTF-8 바이트를 사용하여 원본 폴더나 PC가 달라도 같은 이름은 같은 출력에 덮어쓴다.
			constexpr uint32 OffsetBasis = 2166136261u;
			constexpr uint32 Prime = 16777619u;
			uint32 Hash = OffsetBasis;
			for (unsigned char Byte : Stem) Hash = (Hash ^ Byte) * Prime;
			Name = "FBX_";
			for (int Shift = 28; Shift >= 0; Shift -= 4) Name += "0123456789abcdef"[(Hash >> Shift) & 0xf];
		}
		const auto Folder = std::filesystem::path("Assets/FBX") / Name;
		const auto StaticPath = Folder / (Name + "_Static.fbx.bin");
		const auto SkeletalPath = Folder / (Name + ".fbx.bin");
		TArray<std::filesystem::path> AnimationPaths; // 이번 임포트가 저장하는 애니메이션 파일들
		const auto Primary = Mode == EFbxImportMode::Static ? StaticPath : SkeletalPath;
		FFbxBakedMesh Previous;
		FString ReadError;
		if (!bTestAnimationImport && FFbxMeshBake::Read(Utf8Path(Primary), Previous, ReadError) && SameSource(Source, Previous.Source)
			&& Previous.Kind == (Mode == EFbxImportMode::Static ? EFbxMeshKind::Static : EFbxMeshKind::Skeletal))
		{
			bool Complete = Mode != EFbxImportMode::Static || Previous.Source.Outputs == static_cast<uint32>(EFbxMeshKind::Static);
			const bool bHasAnimation = (Previous.Source.Outputs & FFbxSourceInfo::AnimationOutput) != 0;
			const TArray<std::filesystem::path> ExistingAnimations = FindAnimationFiles(Folder, Name);
			if (Mode == EFbxImportMode::Skeletal)
			{
				// 파일 목록은 따로 기록하지 않으므로, 애니메이션이 있었다면 파일이 하나 이상 있고 모두 읽혀야 완전한 것으로 본다.
				Complete = (Previous.Source.Outputs & FFbxSourceInfo::AnimationChecked) != 0
					&& ExistingAnimations.IsEmpty() != bHasAnimation;
				// 미리보기 메시 경로가 없는 파일(형식 버전 1)은 다시 임포트해서 채운다.
				for (const auto& AnimationFile : ExistingAnimations)
				{
					if (!Complete) break;
					const std::unique_ptr<UAnimSequence> Sequence(NewObject<UAnimSequence>());
					Complete = AnimSequenceFile::Read(AnimationFile, *Sequence, ReadError)
						&& !Sequence->GetPreviewMeshPath().empty();
				}
			}
			TArray<FString> Paths;
			for (EFbxMeshKind Kind : { EFbxMeshKind::Static, EFbxMeshKind::Skeletal })
			{
				if (!(Previous.Source.Outputs & static_cast<uint32>(Kind))) continue;
				const FString OutputPath = Utf8Path(Kind == EFbxMeshKind::Static ? StaticPath : SkeletalPath);
				FFbxBakedMesh Other;
				if (!FFbxMeshBake::Read(OutputPath, Other, ReadError) || !SameSource(Source, Other.Source) || Other.Source.Outputs != Previous.Source.Outputs || Other.Kind != Kind) Complete = false;
				Paths.Add(OutputPath);
			}
			if (Complete)
			{
				// TODO: 현재는 FBX 원본만 비교하므로 외부 Texture만 변경되면 Import를 건너뛰어 변경 내용이 반영되지 않는다.
				// 참조하는 외부 Texture의 변경 정보도 저장·비교하여 이미지가 바뀐 경우 재Import하도록 해야 한다.
				OutResult.bSkipped = true;
				OutResult.Paths = std::move(Paths);
				if (bHasAnimation) for (const auto& AnimationFile : ExistingAnimations) OutResult.AnimationPaths.Add(Utf8Path(AnimationFile));
				HTR_LOG(Info, "FBX Import skipped (source unchanged): {}", Source.Path);
				return RegisterImported(OutResult.Paths, false, OutResult.Warnings, OutError);
			}
		}

		// SDK 추출부터 최종 파일 교체까지 같은 작업 폴더를 유지한다. 실패 시에도 소멸자가 정리한다.
		std::filesystem::create_directories(Folder / "Textures");
		FImportFiles Staged;
		const auto Temporary = Folder / (".import-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
		if (!std::filesystem::create_directory(Temporary)) { OutError = "FBX: temporary directory already exists"; return false; }
		Staged.Folder = Temporary;
		const auto Extraction = std::filesystem::absolute(Temporary / "embedded");
		std::filesystem::create_directory(Extraction);
		FFbxImporter Importer;
		if (!Importer.Open(Source.Path, OutError, Utf8Path(Extraction))) return false;
		if (Mode == EFbxImportMode::Skeletal)
		{
			FbxScene* Scene = Importer.GetScene();
			bool bHasSkinnedMesh = false;
			for (int I = 0; I < Scene->GetNodeCount(); ++I)
			{
				FbxMesh* Mesh = Scene->GetNode(I)->GetMesh();
				if (Mesh && Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0)
				{
					bHasSkinnedMesh = true;
					break;
				}
			}
			if (!bHasSkinnedMesh)
			{
				OutError = "FBX: Skeletal Mesh import requires a mesh with skin weights. Use Static Mesh import for geometry.";
				return false;
			}
		}
		TArray<FString> Warnings;
		if (!Importer.ReadMeshData(Warnings, OutError)) { OutResult.Warnings.Append(Warnings); return false; }
		OutResult.Warnings.Append(Warnings);
		TArray<FFbxBakedMesh> Outputs;
		if (Mode == EFbxImportMode::Skeletal)
		{
			if (!Importer.ReadSkeletonData(Warnings, OutError)) { OutResult.Warnings.Append(Warnings); return false; }
			OutResult.Warnings.Append(Warnings);
			Source.Outputs |= FFbxSourceInfo::AnimationChecked;
			// 스택이 없으면 TArray가 비어 있고, 있으면 ImportAnimations가 모든 스택을 시퀀스로 만든다.
			// 애니메이션 임포트가 실패해도 메시는 저장할 수 있어야 하므로 경고만 남기고 계속한다.
			TArray<std::unique_ptr<UAnimSequence>> Sequences;
			if (Importer.GetScene()->GetSrcObjectCount<FbxAnimStack>() > 0)
			{
				TArray<UAnimSequence*> ImportedSequences;
				FString AnimationError;
				const bool bImported = Importer.ImportAnimations(ImportedSequences, Warnings, AnimationError);
				OutResult.Warnings.Append(Warnings);
				for (UAnimSequence* Imported : ImportedSequences) Sequences.Add(std::unique_ptr<UAnimSequence>(Imported));
				if (!bImported) OutResult.Warnings.Add(AnimationError + "; animation was not imported");
			}
			// 스택마다 파일 하나. 스택 1개면 Name.anim.bin, 여러 개면 Name_스택이름.anim.bin (Unreal ValidTakeCount 규칙).
			// 파일 안의 ClipName은 원본 스택 이름 그대로 두고, 파일 이름에만 Sanitize를 적용한다.
			TSet<FString> UsedAnimationFiles; // 소문자. Windows는 대소문자를 구분하지 않는다.
			bool bAnimationTestPassed = true;
			bool bLogAnimationSamples = true;
			for (int32 SequenceIndex = 0; SequenceIndex < Sequences.Num(); ++SequenceIndex)
			{
				UAnimSequence& Sequence = *Sequences[SequenceIndex];
				const FString Base = Sequences.Num() == 1 ? Name : Name + "_" + SanitizeFileName(Sequence.GetClipName(), "Anim");
				FString File = Base + ".anim.bin";
				for (int32 Suffix = 1; UsedAnimationFiles.Contains(ToLowerAscii(File)); ++Suffix)
				{
					File = Base + "_" + std::to_string(Suffix) + ".anim.bin";
				}
				UsedAnimationFiles.Add(ToLowerAscii(File));

				const auto Target = Folder / File;
				const auto Temp = Staged.Folder / ("animation_" + std::to_string(SequenceIndex) + ".tmp");
				Sequence.SetPreviewMeshPath(Utf8Path(SkeletalPath)); // 애님 시퀀스 뷰어가 이 메시로 미리보기한다

				// 재임포트: 기존 파일의 루트 모션 설정을 이어받는다 (Unreal도 재임포트 때 에셋 설정을 유지한다)
				if (std::filesystem::exists(Target))
				{
					const std::unique_ptr<UAnimSequence> Existing(NewObject<UAnimSequence>());
					FString ExistingError;
					if (AnimSequenceFile::Read(Target, *Existing, ExistingError))
					{
						Sequence.SetEnableRootMotion(Existing->GetEnableRootMotion());
						Sequence.SetForceRootLock(Existing->GetForceRootLock());
					}
				}
				// 다시 임포트할 때 에디터에서 붙인 Notify/트랙을 잃지 않도록 기존 파일에서 옮겨 온다 (UE 재임포트와 같음).
				// 새 길이를 넘는 Notify는 ReplaceNotifies가 잘라 준다.
				std::error_code ExistsError;
				if (std::filesystem::exists(Target, ExistsError))
				{
					std::unique_ptr<UAnimSequence> Previous(NewObject<UAnimSequence>());
					FString PreviousError;
					TArray<uint8> NotifySnapshot;
					if (AnimSequenceFile::Read(Target, *Previous, PreviousError)
						&& !Previous->GetNotifies().IsEmpty()
						&& Previous->CaptureNotifySnapshot(NotifySnapshot)
						&& Sequence.RestoreNotifySnapshot(NotifySnapshot))
					{
						HTR_LOG(Info, "FBX reimport kept {} notifies: {}", Sequence.GetNotifies().Num(), Utf8Path(Target));
					}
				}

				if (!AnimSequenceFile::Write(Temp, Sequence, OutError)) return false;
				Staged.Files.Add({ Temp, Target });
				AnimationPaths.Add(Target);

				if (bTestAnimationImport && !VerifySavedAnimation(Temp, Sequence, Importer.GetBones(), bLogAnimationSamples))
				{
					bAnimationTestPassed = false;
				}
			}
			if (!Sequences.IsEmpty()) Source.Outputs |= FFbxSourceInfo::AnimationOutput;
			if (bTestAnimationImport && !Sequences.IsEmpty())
			{
				if (bAnimationTestPassed) HTR_LOG(Info, "FBX animation test PASS: {} stacks, tracks, key counts and frame metadata match", Sequences.Num());
				else HTR_LOG(Error, "FBX animation test FAIL: tracks, key counts or frame metadata do not match");
			}

			// 이전 임포트가 남긴 애니메이션 파일 중 이번에 다시 쓰지 않는 것은 삭제한다. (스택이 줄었거나 1개↔여러 개로 바뀐 경우)
			for (const auto& OldFile : FindAnimationFiles(Folder, Name))
			{
				if (!UsedAnimationFiles.Contains(ToLowerAscii(Utf8Path(OldFile.filename()))))
				{
					Staged.Files.Add({ std::filesystem::path{}, OldFile });
				}
			}
			FFbxBakedMesh Skeletal;
			Skeletal.Kind = EFbxMeshKind::Skeletal;
			if (!Importer.BuildSkeletalMesh(Skeletal.SkeletalMesh, Warnings, OutError)) { OutResult.Warnings.Append(Warnings); return false; }
			OutResult.Warnings.Append(Warnings);
			Outputs.Add(std::move(Skeletal));
		}
		FFbxBakedMesh Static;
		if (Mode == EFbxImportMode::Static) Importer.BuildStaticMesh(Static.StaticMesh);
		else for (const auto& Mesh : Importer.GetMeshNodes()) if (!Mesh.bHasSkin && Mesh.RigidBoneIndex < 0) AppendGeometry(Mesh.Geometry, Static.StaticMesh);
		if (!Static.StaticMesh.Vertices.IsEmpty())
		{
			if (!Static.StaticMesh.Validate(OutError)) return false;
			Outputs.Add(std::move(Static));
		}
		// Scene을 닫아도 추출 파일은 Staged가 소유하므로 아래 Texture 복사까지 살아 있다.
		Importer.Close();
		if (Outputs.IsEmpty()) { OutError = "FBX: no mesh output"; return false; }
		for (const auto& Output : Outputs) Source.Outputs |= static_cast<uint32>(Output.Kind);
		TMap<FString, FString> Copied;
		TSet<FString> Names;
		for (auto& Output : Outputs)
		{
			Output.Source = Source;
			auto CopyTextures = [&](auto& Slots)
			{
				for (auto& Slot : Slots) for (FString* Texture : { &Slot.DiffuseTexturePath, &Slot.NormalTexturePath })
				{
					if (Texture->empty()) continue;
					const auto Original = std::filesystem::canonical(PathOf(*Texture));
					const FString OriginalKey = Utf8Path(Original);
					if (const auto* Existing = Copied.Find(OriginalKey)) { *Texture = *Existing; continue; }
					FString Extension = Original.extension().string();
					for (char& C : Extension) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
					if (!std::all_of(Extension.begin(), Extension.end(), [](unsigned char C)
					{
						return C == '.' || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9');
					})) Extension = ".texture";
					const FString Base = AssetFileName(Original);
					FString File = Base + Extension;
					int Suffix = 1;
					auto Lower = [](FString Text)
					{
						for (char& C : Text) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
						return Text;
					};
					while (Names.Contains(Lower(File))) File = Base + "_" + std::to_string(Suffix++) + Extension;
					Names.Add(Lower(File));
					const auto Target = Folder / "Textures" / File;
					const auto Temp = Staged.Folder / ("texture_" + std::to_string(Copied.Num()));
					std::filesystem::copy_file(Original, Temp);
					Staged.Files.Add({ Temp, Target });
					*Texture = Utf8Path(Target);
					Copied.Add(OriginalKey, *Texture);
				}
			};
			if (Output.Kind == EFbxMeshKind::Static) CopyTextures(Output.StaticMesh.MaterialSlots);
			else CopyTextures(Output.SkeletalMesh.MaterialSlots);
		}
		TArray<FString> Paths;
		for (const auto& Output : Outputs)
		{
			const auto Target = Output.Kind == EFbxMeshKind::Static ? StaticPath : SkeletalPath;
			// 작업 폴더가 남아도 Asset 검색에서 완성된 FBX Binary로 인식하지 않게 한다.
			const auto Temp = Staged.Folder / (Output.Kind == EFbxMeshKind::Static ? "static.tmp" : "skeletal.tmp");
			if (!FFbxMeshBake::Write(Utf8Path(Temp), Output, OutError)) return false;
			Staged.Files.Add({ Temp, Target });
			Paths.Add(Utf8Path(Target));
		}
		if (Source.Size != std::filesystem::file_size(SourcePath) || Source.ModifiedTime != std::filesystem::last_write_time(SourcePath).time_since_epoch().count()) { OutError = "FBX: source changed during Import; retry"; return false; }
		if (!Staged.Commit(OutError)) return false;
		OutResult.Paths = std::move(Paths);
		if (Source.Outputs & FFbxSourceInfo::AnimationOutput)
		{
			for (const auto& AnimationFile : AnimationPaths) OutResult.AnimationPaths.Add(Utf8Path(AnimationFile));
			HTR_LOG(Info, "FBX animation saved: {} files in {}", OutResult.AnimationPaths.Num(), Utf8Path(Folder));
		}
		HTR_LOG(Info, "FBX Import saved: {} ({} files)", Source.Path, OutResult.Paths.Num());
		return RegisterImported(OutResult.Paths, true, OutResult.Warnings, OutError);
	}
	catch (const std::filesystem::filesystem_error& E)
	{
		OutError = "FBX: file operation failed: " + FString(E.what());
		return false;
	}
}
