#include "EnginePCH.h"
#include "FbxMeshBake.h"

#include <fstream>
#include <cstddef>
#include <type_traits>

namespace
{
	constexpr uint32 Magic = 0x58424648; // HFBX, little endian
	// 장면 전체의 축 변환으로 Bone Local 축과 Bind 행렬 표현이 달라져 기존 출력은 다시 굽는다.
	constexpr uint32 Version = 6;
	constexpr uint32 QuaternionBoneRotationVersion = 5;
	constexpr uint32 EulerBoneRotationVersion = 4; // 본 회전을 FRotator로 저장한 마지막 버전. 그대로 읽어 변환한다
	static_assert(sizeof(FVertex) == 64 && sizeof(FSkinWeight) == 20);
	// 기존 Binary의 Section 배열을 그대로 읽도록 두 타입의 바이트 배치를 고정한다.
	static_assert(std::is_trivially_copyable_v<FSkeletalMeshSection> && std::is_standard_layout_v<FSkeletalMeshSection>);
	static_assert(sizeof(FSkeletalMeshSection) == sizeof(FStaticMeshSection) && sizeof(FSkeletalMeshSection) == 12);
	static_assert(alignof(FSkeletalMeshSection) == alignof(FStaticMeshSection));
	static_assert(offsetof(FSkeletalMeshSection, StartIndex) == offsetof(FStaticMeshSection, StartIndex));
	static_assert(offsetof(FSkeletalMeshSection, IndexCount) == offsetof(FStaticMeshSection, IndexCount));
	static_assert(offsetof(FSkeletalMeshSection, MaterialSlotIndex) == offsetof(FStaticMeshSection, MaterialSlotIndex));

	std::filesystem::path FilePath(const FString& Path)
	{
		return std::filesystem::path(std::u8string(Path.begin(), Path.end()));
	}

	struct FReader
	{
		std::ifstream Stream;
		uint64 Remaining = 0;
		explicit FReader(const FString& Path) : Stream(FilePath(Path), std::ios::binary | std::ios::ate)
		{
			if (Stream && Stream.tellg() >= 0) { Remaining = static_cast<uint64>(Stream.tellg()); Stream.seekg(0); }
		}
		bool Bytes(void* Data, uint64 Size)
		{
			if (Size > Remaining) return false;
			Remaining -= Size;
			if (Size) Stream.read(static_cast<char*>(Data), static_cast<std::streamsize>(Size));
			return static_cast<bool>(Stream);
		}
		template<typename T> bool Pod(T& Value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			return Bytes(&Value, sizeof(T));
		}
		bool Count(uint32& Value, uint64 MinBytes)
		{
			return Pod(Value) && Value <= INT32_MAX && Value <= Remaining / MinBytes;
		}
		template<typename T> bool Array(TArray<T>& Value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			uint32 N;
			if (!Count(N, sizeof(T))) return false;
			Value.SetNum(static_cast<int32>(N));
			return Bytes(Value.GetData(), uint64(N) * sizeof(T));
		}
		bool String(FString& Value)
		{
			uint32 N;
			if (!Count(N, 1)) return false;
			Value.resize(N);
			return Bytes(Value.data(), N) && Value.find('\0') == FString::npos;
		}
	};

	struct FWriter
	{
		std::ofstream Stream;
		explicit FWriter(const FString& Path) : Stream(FilePath(Path), std::ios::binary | std::ios::trunc) {}
		template<typename T> void Pod(const T& Value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			Stream.write(reinterpret_cast<const char*>(&Value), sizeof(T));
		}
		template<typename T> void Array(const TArray<T>& Value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			Pod(static_cast<uint32>(Value.Num()));
			if (!Value.IsEmpty()) Stream.write(reinterpret_cast<const char*>(Value.GetData()), sizeof(T) * Value.Num());
		}
		void String(const FString& Value)
		{
			if (Value.size() > INT32_MAX) { Stream.setstate(std::ios::failbit); return; }
			Pod(static_cast<uint32>(Value.size()));
			Stream.write(Value.data(), Value.size());
		}
	};

	template<typename TSlot>
	bool ReadSlots(FReader& In, TArray<TSlot>& Slots)
	{
		uint32 N;
		if (!In.Count(N, 3 * sizeof(uint32) + sizeof(FVector4))) return false;
		Slots.SetNum(static_cast<int32>(N));
		for (auto& S : Slots)
			if (!In.String(S.Name) || !In.Pod(S.BaseColor) || !In.String(S.DiffuseTexturePath) || !In.String(S.NormalTexturePath)) return false;
		return true;
	}

	bool ReadPayload(FReader& In, FFbxBakedMesh& D, uint32 FileVersion)
	{
		auto& Vertices = D.Kind == EFbxMeshKind::Static ? D.StaticMesh.Vertices : D.SkeletalMesh.BindVertices;
		auto& Indices = D.Kind == EFbxMeshKind::Static ? D.StaticMesh.Indices : D.SkeletalMesh.Indices;
		auto& Bounds = D.Kind == EFbxMeshKind::Static ? D.StaticMesh.AABB : D.SkeletalMesh.BindAABB;
		auto ReadGeometry = [&](auto& Sections, auto& Slots)
		{
			if (!In.Array(Vertices) || !In.Array(Indices) || !In.Array(Sections) || !ReadSlots(In, Slots) || !In.Pod(Bounds)) return false;
			// 파일의 Section 범위를 넓은 정수로 검사해 uint32 덧셈의 overflow를 피한다.
			for (const auto& Section : Sections)
				if (uint64(Section.StartIndex) + Section.IndexCount > static_cast<uint64>(Indices.Num())) return false;
			return true;
		};
		if (!(D.Kind == EFbxMeshKind::Static ? ReadGeometry(D.StaticMesh.Sections, D.StaticMesh.MaterialSlots)
			: ReadGeometry(D.SkeletalMesh.Sections, D.SkeletalMesh.MaterialSlots))) return false;
		if (D.Kind == EFbxMeshKind::Skeletal)
		{
			uint32 N;
			if (!In.Array(D.SkeletalMesh.SkinWeight) || !In.Count(N, sizeof(uint32) + sizeof(int32) + 25 * sizeof(float)) || N == 0 || N > 256) return false;
			D.SkeletalMesh.Bones.SetNum(static_cast<int32>(N));
			for (auto& B : D.SkeletalMesh.Bones)
			{
				if (!In.String(B.Name) || !In.Pod(B.ParentIndex) || !In.Pod(B.LocalBindPose.Location)) return false;
				if (FileVersion == EulerBoneRotationVersion)
				{
					FRotator Euler;
					if (!In.Pod(Euler)) return false;
					B.LocalBindPose.Rotation = Euler.Quaternion();
				}
				else if (!In.Pod(B.LocalBindPose.Rotation)) return false;
				if (!In.Pod(B.LocalBindPose.Scale) || !In.Pod(B.InverseBindPose.M)) return false;
			}
			D.SkeletalMesh.SourcePath = D.Source.Path;
		}
		return In.Remaining == 0;
	}

	// 파일의 비유한 숫자는 거부하고 Index/Weight/Section 검사는 기존 Validate를 사용한다.
	bool FinitePayload(const FFbxBakedMesh& D)
	{
		const auto& Vertices = D.Kind == EFbxMeshKind::Static ? D.StaticMesh.Vertices : D.SkeletalMesh.BindVertices;
		for (const auto& V : Vertices)
		{
			const float Values[] = { V.Position.X,V.Position.Y,V.Position.Z,V.Normal.X,V.Normal.Y,V.Normal.Z,V.Tangent.X,V.Tangent.Y,V.Tangent.Z,V.Tangent.W,V.UV.X,V.UV.Y,V.Color.X,V.Color.Y,V.Color.Z,V.Color.W };
			for (float X : Values) if (!std::isfinite(X)) return false;
		}
		const auto& Bounds = D.Kind == EFbxMeshKind::Static ? D.StaticMesh.AABB : D.SkeletalMesh.BindAABB;
		for (int I = 0; I < 3; ++I) if (!std::isfinite(Bounds.Min[I]) || !std::isfinite(Bounds.Max[I]) || Bounds.Min[I] > Bounds.Max[I]) return false;
		auto FiniteSlots = [](const auto& Slots)
		{
			for (const auto& S : Slots) for (int I = 0; I < 4; ++I) if (!std::isfinite(S.BaseColor[I])) return false;
			return true;
		};
		if (!(D.Kind == EFbxMeshKind::Static ? FiniteSlots(D.StaticMesh.MaterialSlots) : FiniteSlots(D.SkeletalMesh.MaterialSlots))) return false;
		if (D.Kind == EFbxMeshKind::Skeletal)
		{
			for (const auto& W : D.SkeletalMesh.SkinWeight) for (int I = 0; I < MaxBoneInfluences; ++I)
				if (!std::isfinite(W.Weight[I]) || W.Weight[I] < 0 || W.BoneIndex[I] >= D.SkeletalMesh.Bones.Num()) return false;
			for (const auto& B : D.SkeletalMesh.Bones)
			{
				for (int I = 0; I < 3; ++I) if (!std::isfinite(B.LocalBindPose.Location[I]) || !std::isfinite(B.LocalBindPose.Scale[I])) return false;
				for (int I = 0; I < 4; ++I) if (!std::isfinite(B.LocalBindPose.Rotation.V[I])) return false;
				for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C) if (!std::isfinite(B.InverseBindPose.M[R][C])) return false;
			}
		}
		return true;
	}
}

bool FFbxMeshBake::IsBakedPath(const FString& Path)
{
	// 파일 이름 끝이 .fbx.bin인지 대소문자를 구분하지 않고 확인한다.
	// 실제 파일 내부의 Magic과 Version은 Read에서 검사한다.
	FString End = Path.size() >= 8 ? Path.substr(Path.size() - 8) : FString();
	for (char& C : End) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
	return End == ".fbx.bin";
}

bool FFbxMeshBake::Read(const FString& Path, FFbxBakedMesh& OutData, FString& OutError)
{
	OutData = {};
	OutError.clear();
	FReader In(Path);
	FFbxBakedMesh D;
	uint32 M = 0, V = 0;
	if (!In.Pod(M) || M != Magic || !In.Pod(V)) { OutError = "FBX binary: invalid header: " + Path; return false; }
	if (V != Version && V != QuaternionBoneRotationVersion && V != EulerBoneRotationVersion)
	{
		OutError = "FBX binary: unsupported version " + std::to_string(V) + " (expected " + std::to_string(Version) + "); reimport the source FBX: " + Path;
		return false;
	}
	if (!In.Pod(D.Kind) || (D.Kind != EFbxMeshKind::Static && D.Kind != EFbxMeshKind::Skeletal)
		|| !In.Pod(D.Source.Mode) || (D.Source.Mode != EFbxImportMode::Static && D.Source.Mode != EFbxImportMode::Skeletal)
		|| !In.Pod(D.Source.Outputs) || !(D.Source.Outputs & static_cast<uint32>(D.Kind))
		|| (D.Source.Outputs & ~(3u | FFbxSourceInfo::AnimationChecked | FFbxSourceInfo::AnimationOutput))
		|| ((D.Source.Outputs & FFbxSourceInfo::AnimationOutput) && !(D.Source.Outputs & FFbxSourceInfo::AnimationChecked))
		|| (D.Source.Mode == EFbxImportMode::Static && D.Source.Outputs != static_cast<uint32>(EFbxMeshKind::Static))
		|| !In.String(D.Source.Path) || !In.Pod(D.Source.ModifiedTime) || !In.Pod(D.Source.Size))
	{
		OutError = "FBX binary: invalid header: " + Path;
		return false;
	}
	if (!ReadPayload(In, D, V) || !FinitePayload(D)) { OutError = "FBX binary: invalid/truncated payload: " + Path; return false; }
	if (D.Kind == EFbxMeshKind::Static ? !D.StaticMesh.Validate(OutError) : !D.SkeletalMesh.Validate(OutError)) return false;
	OutData = std::move(D);
	return true;
}

bool FFbxMeshBake::Write(const FString& Path, const FFbxBakedMesh& D, FString& OutError)
{
	OutError.clear();
	FWriter Out(Path);
	Out.Pod(Magic); Out.Pod(Version); Out.Pod(D.Kind); Out.Pod(D.Source.Mode); Out.Pod(D.Source.Outputs);
	Out.String(D.Source.Path); Out.Pod(D.Source.ModifiedTime); Out.Pod(D.Source.Size);
	const auto& Vertices = D.Kind == EFbxMeshKind::Static ? D.StaticMesh.Vertices : D.SkeletalMesh.BindVertices;
	const auto& Indices = D.Kind == EFbxMeshKind::Static ? D.StaticMesh.Indices : D.SkeletalMesh.Indices;
	auto WriteGeometry = [&](const auto& Sections, const auto& Slots)
	{
		Out.Array(Vertices); Out.Array(Indices); Out.Array(Sections);
		Out.Pod(static_cast<uint32>(Slots.Num()));
		for (const auto& S : Slots) { Out.String(S.Name); Out.Pod(S.BaseColor); Out.String(S.DiffuseTexturePath); Out.String(S.NormalTexturePath); }
	};
	if (D.Kind == EFbxMeshKind::Static) WriteGeometry(D.StaticMesh.Sections, D.StaticMesh.MaterialSlots);
	else WriteGeometry(D.SkeletalMesh.Sections, D.SkeletalMesh.MaterialSlots);
	Out.Pod(D.Kind == EFbxMeshKind::Static ? D.StaticMesh.AABB : D.SkeletalMesh.BindAABB);
	if (D.Kind == EFbxMeshKind::Skeletal)
	{
		Out.Array(D.SkeletalMesh.SkinWeight);
		Out.Pod(static_cast<uint32>(D.SkeletalMesh.Bones.Num()));
		for (const auto& B : D.SkeletalMesh.Bones)
		{
			Out.String(B.Name); Out.Pod(B.ParentIndex); Out.Pod(B.LocalBindPose.Location); Out.Pod(B.LocalBindPose.Rotation); Out.Pod(B.LocalBindPose.Scale); Out.Pod(B.InverseBindPose.M);
		}
	}
	Out.Stream.flush();
	Out.Stream.close();
	if (!Out.Stream) { OutError = "FBX binary: write failed: " + Path; return false; }
	return true;
}
