#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "PoseRetargetPanel.h"
#include "StaticPoseRetargeter.h"
#include "Asset/AssetManager.h"
#include "Asset/AnimSequence.h"
#include "Asset/Skeleton.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Engine/World.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Render/Texture2D.h"
#include "Input/InputSystem.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "UObject/UObjectIterator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_set>

namespace
{
	bool IsFbxPath(const std::filesystem::path& Path)
	{
		for (const auto& Part : Path) if (Part.native().starts_with(L".")) return false;
		FString Extension = Path.extension().string();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			[](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
		// 스켈레탈 메시는 바이너리로만 고른다. .fbx는 먼저 임포트해야 한다.
		if (Extension == ".fbxbin")
		{
			FFbxAssetData Data; FString Error;
			fs::path File = Path;
			if (!fs::is_regular_file(File)) File = fs::path("Assets") / File;
			return FFbxAssetBake::ReadHeader(FbxPath::ToString(File), Data, Error) && Data.Type == EFbxAssetType::SkeletalMesh;
		}
		return false;
	}

	FString BoneMatchKey(const FString& Name)
	{
		const auto ReadNumber = [](const FString& Value, size_t Start)
		{
			int32 Result = 0;
			for (size_t Index = Start; Index < Value.size(); ++Index)
				Result = std::min(1000000, Result * 10 + Value[Index] - '0');
			return Result;
		};
		TArray<FString> Tokens;
		FString Token;
		for (unsigned char Character : Name)
		{
			if (std::isalnum(Character)) Token += static_cast<char>(std::tolower(Character));
			else if (!Token.empty()) { Tokens.Add(std::move(Token)); Token.clear(); }
		}
		if (!Token.empty()) Tokens.Add(std::move(Token));
		FString Side;
		FString Base;
		int32 Number = -1;
		for (int32 Index = 0; Index < Tokens.Num(); ++Index)
		{
			const FString& Part = Tokens[Index];
			if (Part == "left" || Part == "l") { Side = "left"; continue; }
			if (Part == "right" || Part == "r") { Side = "right"; continue; }
			const size_t LastLetter = Part.find_last_not_of("0123456789");
			if (LastLetter == FString::npos)
			{
				Number = ReadNumber(Part, 0);
				continue;
			}
			if (Part == "bn" || Part == "m" || Part == "bip001") continue;
			Base = Part;
			Number = -1;
			if (LastLetter + 1 < Part.size())
			{
				Base = Part.substr(0, LastLetter + 1);
				Number = ReadNumber(Part, LastLetter + 1);
			}
		}
		return Base.empty() ? FString() : Base + "|" + Side + "|" + std::to_string(Number);
	}
}

bool FPoseRetargetPanel::Init()
{
	RefreshFbxFiles();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		PreviewWorlds[Index] = UWorld::CreateWorld(EWorldType::Editor, false);
		if (!PreviewWorlds[Index]) return false;
		ASkeletalMeshActor* Actor = PreviewWorlds[Index]->SpawnActor<ASkeletalMeshActor>();
		if (!Actor) return false;
		Components[Index] = Actor->GetSkeletalMeshComponent();
		Components[Index]->SetSkeletalMesh(nullptr);
		if (ASkyLightActor* SkyLight = PreviewWorlds[Index]->SpawnActor<ASkyLightActor>())
			SkyLight->GetBillboardComponent()->SetVisibility(false);
		Viewports[Index].SetWorld(PreviewWorlds[Index]);
		Viewports[Index].SetViewportType(ELevelViewportType::Perspective);
		Viewports[Index].Resize(1, 1);
		Viewports[Index].SetActive(true);
	}
	return true;
}

void FPoseRetargetPanel::RefreshFbxFiles()
{
	AvailableFbxPaths.Reset();
	if (!std::filesystem::exists("Assets")) return;
	for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator("Assets"))
		if (Entry.is_regular_file() && IsFbxPath(Entry.path()))
			AvailableFbxPaths.Add(std::filesystem::relative(Entry.path(), "Assets").generic_string());
}

void FPoseRetargetPanel::Release()
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Viewports[Index].SetWorld(nullptr);
		if (PreviewWorlds[Index])
		{
			PreviewWorlds[Index]->ClearWorld();
			PreviewWorlds[Index]->FlushPendingDestruction();
		}
		PreviewWorlds[Index] = nullptr;
		Components[Index] = nullptr;
	}
	SourceRig = nullptr;
	TargetMesh = nullptr;
}

void FPoseRetargetPanel::FitCamera(int32 Index)
{
	USkeletalMesh* Mesh = Components[Index] ? Components[Index]->GetSkeletalMesh() : nullptr;
	if (!Mesh) return;
	const FBox& Bounds = Mesh->MeshData.AABB;
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = std::max(0.5f, (Bounds.Max - Bounds.Min).Size() * 0.5f);
	const float HalfFov = FMath::DegreesToRadians(Viewports[Index].GetViewFOV()) * 0.5f;
	const float Distance = Radius / std::max(0.2f, std::sin(HalfFov)) * 1.5f;
	Viewports[Index].SetViewLocation(Center - Viewports[Index].GetViewForward() * Distance);
}

void FPoseRetargetPanel::SetRig(int32 Index, USkeletalMesh* Mesh)
{
	if (Index == 0)
	{
		if (SourceRig != Mesh) // Source FBX 바뀌었는 지 확인
		{
			for (int32& Bone : TargetToSource) Bone = -1;
			if (Components[2]) Components[2]->InitBoneTransforms(); // 다시 target fbx로 초기화
			AnimationMessage.clear();
			SelectedAnimation = -1;
			LastAnimationFrame = -1;
			AnimationTime = 0.0f;
			bAnimationPlaying = false;
			bResultPoseApplied = false;
			SelectedSequence = nullptr;
			RootCorrections.Reset();
		}
		SourceRig = Mesh;
	}
	else TargetMesh = Mesh;

	Components[Index]->SetSkeletalMesh(Mesh);

	if (Index == 1)
	{
		bResultPoseApplied = false;
		Components[2]->SetSkeletalMesh(Mesh);
		TargetToSource.SetNum(Mesh ? Mesh->MeshData.Bones.Num() : 0);
		for (int32& Bone : TargetToSource) Bone = -1;
		TargetAimChild.SetNum(Mesh ? Mesh->MeshData.Bones.Num() : 0);
		for (int32& Bone : TargetAimChild) Bone = -1;
		FitCamera(2);
	}
	FitCamera(Index);
	Message.clear();
}

void FPoseRetargetPanel::Tick(float DeltaTime)
{
	if (!IsOpen()) return;
	for (int32 Index = 0; Index < 3; ++Index)
		if (PreviewWorlds[Index]) PreviewWorlds[Index]->Tick(DeltaTime);
	if (bAnimationPlaying && SourceRig && SelectedAnimation >= 0)
	{
		const float Duration = SelectedSequence && SelectedSequence->GetDataModel() ? SelectedSequence->GetDataModel()->PlayLength : 0.0f;
		if (Duration > 0.0f)
		{
			AnimationTime = std::fmod(AnimationTime + std::max(0.0f, DeltaTime), Duration);
			ApplyAnimationFrame();
		}
	}
}

void FPoseRetargetPanel::SelectAnimation(int32 ClipIndex)
{
	bAnimationPlaying = false;
	AnimationTime = 0.0f;
	LastAnimationFrame = -1;
	SelectedAnimation = -1;
	SelectedSequence = nullptr;
	RootCorrections.Reset();
	if (!SourceRig || !Components[0]) return;
	if (ClipIndex < 0)
	{
		Components[0]->InitBoneTransforms();
		if (bResultPoseApplied && Components[2])
			ApplyStaticRigPose(*SourceRig, *Components[2], TargetToSource, TargetAimChild, SourceYaw, Message);
		return;
	}
	const TArray<UAnimSequence*>* Sequences = UAssetManager::GetFbxAnimSequences(SourceRig->GetPath());
	if (!Sequences || ClipIndex >= Sequences->Num()) return;
	UAnimSequence* Sequence = (*Sequences)[ClipIndex];
	if (!Sequence || !Sequence->Skeleton || !Sequence->GetDataModel()) return;
	const TArray<FBoneInfo>& Bones = SourceRig->MeshData.Bones;
	const FReferenceSkeleton& Reference = Sequence->Skeleton->RefSkeleton;
	TArray<FMatrix> FirstFrame;
	if (Reference.GetNum() != Bones.Num() || !Sequence->EvaluateGlobalPose(0.0f, FirstFrame) || FirstFrame.Num() != Bones.Num())
	{
		Components[0]->InitBoneTransforms();
		AnimationMessage = "Animation skeleton does not match Source FBX.";
		return;
	}
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
		if (Reference.RefBoneInfo[Index].Name != Bones[Index].Name || Reference.RefBoneInfo[Index].ParentIndex != Bones[Index].ParentIndex)
		{
			Components[0]->InitBoneTransforms();
			AnimationMessage = "Animation skeleton does not match Source FBX.";
			return;
		}
	RootCorrections.SetNum(Bones.Num());
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const int32 Parent = Bones[Index].ParentIndex;
		RootCorrections[Index] = Parent < 0 ?
			FirstFrame[Index].Inverse() * Bones[Index].GlobalBindPose : RootCorrections[Parent];
	}
	SelectedSequence = Sequence;
	SelectedAnimation = ClipIndex;
	AnimationMessage.clear();
	ApplyAnimationFrame();
}

void FPoseRetargetPanel::ApplyAnimationFrame()
{
	if (!SourceRig || !Components[0] || SelectedAnimation < 0) return;
	if (!SelectedSequence || !SelectedSequence->GetDataModel()) return;
	const UAnimDataModel* Model = SelectedSequence->GetDataModel();
	const float FrameRate = Model->FrameRate.AsDecimal();
	if (Model->NumberOfKeys <= 0 || FrameRate <= 0.0f) return;
	const int32 FrameIndex = AnimationTime >= Model->PlayLength ? Model->NumberOfKeys - 1 :
		std::clamp(static_cast<int32>(AnimationTime * FrameRate), 0, Model->NumberOfKeys - 1);
	if (FrameIndex == LastAnimationFrame) return;
	TArray<FMatrix> GlobalPose;
	if (!SelectedSequence->EvaluateGlobalPose(FrameIndex / FrameRate, GlobalPose) || GlobalPose.Num() != RootCorrections.Num()) return;
	for (int32 BoneIndex = 0; BoneIndex < GlobalPose.Num(); ++BoneIndex)
		GlobalPose[BoneIndex] = GlobalPose[BoneIndex] * RootCorrections[BoneIndex];
	Components[0]->SetGlobalBoneTransforms(GlobalPose);
	Components[0]->UpdateSkinning();
	if (bResultPoseApplied && Components[2] && TargetMesh)
	{
		FString Error;
		if (!ApplyAnimatedRigPose(*SourceRig, Components[0]->GetGlobalBoneTransforms(), *Components[2],
			TargetToSource, TargetAimChild, SourceYaw, Error))
		{
			Message = Error;
			bResultPoseApplied = false;
		}
	}
	LastAnimationFrame = FrameIndex;
}

void FPoseRetargetPanel::DrawRigPicker(int32 Index)
{
	const char* Label = Index == 0 ? "Source FBX" : "Target FBX";
	USkeletalMesh* Selected = Index == 0 ? SourceRig : TargetMesh;
	const FString Current = Selected ? Selected->GetPath() : "Select FBX";
	ImGui::SetNextItemWidth(std::max(250.0f, ImGui::GetContentRegionAvail().x - 120.0f));
	if (!ImGui::BeginCombo(Label, Current.c_str())) return;

	// Loaded meshes are reused; FBX files found on disk are imported only when chosen.
	std::map<FString, USkeletalMesh*> Options;
	for (TObjectIterator<USkeletalMesh> It; It; ++It)
	{
		USkeletalMesh* Mesh = *It;
		if (Mesh->IsRetired()) continue;
		if (Mesh && !Mesh->GetPath().empty() && IsFbxPath(Mesh->GetPath()))
			Options[Mesh->GetPath()] = Mesh;
	}
	for (const FString& Key : AvailableFbxPaths)
		if (Options.find(Key) == Options.end()) Options[Key] = nullptr;

	for (const auto& [Key, LoadedMesh] : Options)
	{
		if (!ImGui::Selectable(Key.c_str(), Selected && Selected->GetPath() == Key)) continue;
		USkeletalMesh* Mesh = LoadedMesh ? LoadedMesh :
			UAssetManager::LoadFbxSkeletalMesh((std::filesystem::path("Assets") / Key).generic_string());
		if (Mesh) SetRig(Index, Mesh);
		else Message = "Could not load the selected FBX.";
	}
	ImGui::EndCombo();
}

void FPoseRetargetPanel::DrawViewport(int32 Index, const char* Label)
{
	ImGui::PushID(Index);
	USkeletalMesh* Mesh = Components[Index] ? Components[Index]->GetSkeletalMesh() : nullptr;
	if (Mesh) ImGui::Text("%s (%d bones)", Label, Mesh->MeshData.Bones.Num());
	else ImGui::TextUnformatted(Label);
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const ImVec2 Size(std::max(1.0f, Available.x), std::max(1.0f, Available.y - 2.0f));
	Viewports[Index].Resize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
	Viewports[Index].SetRect(FRect{ 0, 0, Size.x, Size.y });
	FTexture2D* Color = Viewports[Index].GetColorTarget();
	if (Color && Color->GetSRV())
		ImGui::Image((ImTextureID)Color->GetSRV(), Size);
	else
		ImGui::Dummy(Size);
	if (ImGui::IsItemHovered())
	{
		const float Wheel = ImGui::GetIO().MouseWheel;
		const bool bCaptured = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		// 우클릭으로 카메라를 잡은 동안 휠은 이동 속도를 바꾼다.
		if (bCaptured && Wheel != 0.0f)
		{
			CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
		}
		else if (Wheel != 0.0f)
		{
			const FVector Center = Mesh
				? Components[Index]->GetWorldMatrix().TransformPosition(
					(Mesh->MeshData.AABB.Min + Mesh->MeshData.AABB.Max) * 0.5f)
				: FVector::ZeroVector;
			const FVector Location = Viewports[Index].GetViewLocation();
			const float Distance = Mesh ? (Center - Location).Size() : 5.0f;
			float Step = Wheel * std::max(0.05f, Distance * 0.12f);
			if (Step > 0.0f) Step = std::min(Step, Distance * 0.8f);
			Viewports[Index].SetViewLocation(Location + Viewports[Index].GetViewForward() * Step);
		}
		Viewports[Index].TickInputFromImGui(ImGui::GetIO().DeltaTime,
			bCaptured, true, 0, 10.0f * CameraSpeed, 0.2f);
	}
	ImGui::PopID();
}

void FPoseRetargetPanel::DrawMapping()
{
	if (!SourceRig || !TargetMesh) return;
	const TArray<FBoneInfo>& Source = SourceRig->MeshData.Bones;
	const TArray<FBoneInfo>& Target = TargetMesh->MeshData.Bones;
	ImGui::SeparatorText("Target bone -> Source bone");
	ImGui::TextDisabled("Map parent and child bones. Aim child selects the branch that controls the parent direction.");
	if (ImGui::Button("Match names and numbers"))
	{
		std::unordered_set<int32> Used;
		for (int32 Mapped : TargetToSource) if (Mapped >= 0) Used.insert(Mapped);
		for (int Pass = 0; Pass < 2; ++Pass)
			for (int32 TargetIndex = 0; TargetIndex < Target.Num(); ++TargetIndex)
			{
				if (TargetToSource[TargetIndex] >= 0) continue;
				const FString Key = Pass == 0 ? Target[TargetIndex].Name : BoneMatchKey(Target[TargetIndex].Name);
				if (Key.empty()) continue;
				int32 Candidate = -1;
				bool bAmbiguous = false;
				for (int32 SourceIndex = 0; SourceIndex < Source.Num(); ++SourceIndex)
				{
					if (Used.contains(SourceIndex)) continue;
					const FString SourceKey = Pass == 0 ? Source[SourceIndex].Name : BoneMatchKey(Source[SourceIndex].Name);
					if (Key != SourceKey) continue;
					if (Candidate >= 0) { bAmbiguous = true; break; }
					Candidate = SourceIndex;
				}
				if (Candidate >= 0 && !bAmbiguous)
				{
					TargetToSource[TargetIndex] = Candidate;
					Used.insert(Candidate);
				}
			}
		LastAnimationFrame = -1;
		if (SelectedAnimation >= 0 && bResultPoseApplied) ApplyAnimationFrame();
	}
	float TargetColumnX = EditorUIScale::Px(220.0f);
	float SourceWidth = EditorUIScale::Px(200.0f);
	float AimWidth = EditorUIScale::Px(140.0f);

	for (int32 Index = 0; Index < Target.Num(); ++Index)
	{
		int32 Depth = 0;
		for (int32 Parent = Target[Index].ParentIndex; Parent >= 0 && Parent < Index;
			Parent = Target[Parent].ParentIndex) ++Depth;
		TargetColumnX = std::max(TargetColumnX,
			ImGui::CalcTextSize(Target[Index].Name.c_str()).x + std::min(Depth, 6) * EditorUIScale::Px(10.0f) + EditorUIScale::Px(24.0f));
		AimWidth = std::max(AimWidth, ImGui::CalcTextSize(Target[Index].Name.c_str()).x + EditorUIScale::Px(40.0f));
	}

	for (const FBoneInfo& Bone : Source)
		SourceWidth = std::max(SourceWidth, ImGui::CalcTextSize(Bone.Name.c_str()).x + EditorUIScale::Px(40.0f));
	ImGui::BeginChild("Bone mapping", ImVec2(0, std::max(EditorUIScale::Px(240.0f), ImGui::GetWindowSize().y - EditorUIScale::Px(180.0f))),
		true, ImGuiWindowFlags_HorizontalScrollbar);

	for (int32 TargetIndex = 0; TargetIndex < Target.Num(); ++TargetIndex)
	{
		ImGui::PushID(TargetIndex);
		int32 Depth = 0;
		for (int32 Parent = Target[TargetIndex].ParentIndex; Parent >= 0 && Parent < TargetIndex;
			Parent = Target[Parent].ParentIndex) ++Depth;
		ImGui::Indent(EditorUIScale::Px(static_cast<float>(std::min(Depth, 6) * 10)));
		ImGui::TextUnformatted(Target[TargetIndex].Name.c_str());
		ImGui::SameLine(TargetColumnX);
		const int32 Mapped = TargetToSource[TargetIndex];
		const char* Current = Mapped >= 0 && Mapped < Source.Num() ? Source[Mapped].Name.c_str() : "Unmapped";

		ImGui::SetNextItemWidth(SourceWidth);
		if (ImGui::BeginCombo("##SourceBone", Current))
		{
			if (ImGui::Selectable("Unmapped", Mapped < 0)) TargetToSource[TargetIndex] = -1;
			for (int32 SourceIndex = 0; SourceIndex < Source.Num(); ++SourceIndex)
				if (ImGui::Selectable(Source[SourceIndex].Name.c_str(), Mapped == SourceIndex))
					TargetToSource[TargetIndex] = SourceIndex;
			ImGui::EndCombo();
		}

		ImGui::SameLine(TargetColumnX + SourceWidth + ImGui::GetStyle().ItemSpacing.x);
		const int32 Aim = TargetAimChild[TargetIndex];
		const char* AimLabel = Aim >= 0 && Aim < Target.Num() ? Target[Aim].Name.c_str() : "Auto child";
		ImGui::SetNextItemWidth(AimWidth);
		if (ImGui::BeginCombo("##AimChild", AimLabel))
		{
			if (ImGui::Selectable("Auto child", Aim < 0)) TargetAimChild[TargetIndex] = -1;
			for (int32 Child = TargetIndex + 1; Child < Target.Num(); ++Child)
				if (Target[Child].ParentIndex == TargetIndex &&
					ImGui::Selectable(Target[Child].Name.c_str(), Aim == Child))
					TargetAimChild[TargetIndex] = Child;
			ImGui::EndCombo();
		}
		ImGui::Unindent(EditorUIScale::Px(static_cast<float>(std::min(Depth, 6) * 10)));
		ImGui::PopID();
	}
	ImGui::EndChild();
	const char* YawLabels[] = { "0 degrees", "90 degrees", "180 degrees", "270 degrees" };
	if (ImGui::Combo("Source forward yaw", &SourceYaw, YawLabels, 4))
	{
		LastAnimationFrame = -1;
		if (SelectedAnimation >= 0) ApplyAnimationFrame();
	}
	if (ImGui::Button("Apply source rest pose"))
	{
		bResultPoseApplied = ApplyStaticRigPose(*SourceRig, *Components[2], TargetToSource, TargetAimChild, SourceYaw, Message); // 실제로 맵핑된 정보로 target mesh를 source mesh pose 로 적용
		if (bResultPoseApplied)
		{
			Message = "Source rest pose applied to the target mesh.";
			LastAnimationFrame = -1;
			if (SelectedAnimation >= 0) ApplyAnimationFrame();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset target pose"))
	{
		bResultPoseApplied = false;
		Components[2]->InitBoneTransforms();
	}
	ImGui::SetNextItemWidth(std::max(250.0f, ImGui::GetContentRegionAvail().x - 140.0f));
	if (ImGui::BeginCombo("Mapping file", ProfileName))
	{
		std::map<FString, bool> Profiles;
		std::error_code Error;
		const std::filesystem::path Directory(EditorResourcePaths::RetargetProfileFolder);
		for (std::filesystem::directory_iterator It(Directory, Error), End; !Error && It != End; It.increment(Error))
			if (It->is_regular_file() && It->path().extension() == ".json")
				Profiles.emplace(It->path().stem().string(), true);
		for (const auto& Profile : Profiles)
			if (ImGui::Selectable(Profile.first.c_str(), Profile.first == ProfileName))
			{
				const size_t Length = std::min(Profile.first.size(), sizeof(ProfileName) - 1);
				std::copy_n(Profile.first.begin(), Length, ProfileName);
				ProfileName[Length] = '\0';
			}
		ImGui::EndCombo();
	}
	ImGui::InputText("Profile name", ProfileName, sizeof(ProfileName));
	const FString Name(ProfileName);
	const bool bValidName = !Name.empty() && Name.find_first_not_of(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == FString::npos;
	const std::filesystem::path ProfileFile = std::filesystem::path(EditorResourcePaths::RetargetProfileFolder) / (Name + ".json");
	if (!bValidName) ImGui::TextUnformatted("Use letters, numbers, _ or - in the profile name.");
	ImGui::BeginDisabled(!bValidName);

	if (ImGui::Button("Save mapping"))
	{
		json File;
		File["Version"] = 1;
		File["SourceFBX"] = SourceRig->GetPath();
		File["TargetFBX"] = TargetMesh->GetPath();
		File["SourceYaw"] = SourceYaw;
		File["Pairs"] = json::array();
		for (int32 TargetIndex = 0; TargetIndex < Target.Num(); ++TargetIndex)
			if (TargetToSource[TargetIndex] >= 0)
				File["Pairs"].push_back({ {"Target", Target[TargetIndex].Name},
					{"Source", Source[TargetToSource[TargetIndex]].Name},
					{"AimChild", TargetAimChild[TargetIndex] >= 0 ? Target[TargetAimChild[TargetIndex]].Name : FString()} });
		std::filesystem::create_directories(ProfileFile.parent_path());
		std::ofstream Out(ProfileFile);
		if (Out) { Out << File.dump(2); Message = "Mapping saved."; }
		else Message = "Could not save mapping.";
	}
	ImGui::SameLine();
	if (ImGui::Button("Load mapping"))
	{
		std::ifstream In(ProfileFile);
		const json File = In ? json::parse(In, nullptr, false) : json();
		if (File.is_discarded() || !File.is_object() || !File.contains("Pairs") ||
			!File["Pairs"].is_array() || !File.contains("SourceFBX") || !File["SourceFBX"].is_string() ||
			!File.contains("TargetFBX") || !File["TargetFBX"].is_string() ||
			File["SourceFBX"].get<FString>() != SourceRig->GetPath() ||
			File["TargetFBX"].get<FString>() != TargetMesh->GetPath())
			Message = "Mapping file is invalid or belongs to different FBX assets.";
		else
		{
			for (int32& Bone : TargetToSource) Bone = -1;
			for (int32& Bone : TargetAimChild) Bone = -1;
			for (const json& Pair : File["Pairs"])
			{
				if (!Pair.is_object() || !Pair.contains("Target") || !Pair.contains("Source") ||
					!Pair["Target"].is_string() || !Pair["Source"].is_string()) continue;
				const FString TargetName = Pair["Target"].get<FString>();
				const FString SourceName = Pair["Source"].get<FString>();
				const FString AimName = Pair.contains("AimChild") && Pair["AimChild"].is_string()
					? Pair["AimChild"].get<FString>() : FString();
				for (int32 TargetIndex = 0; TargetIndex < Target.Num(); ++TargetIndex)
					if (Target[TargetIndex].Name == TargetName)
					{
						for (int32 SourceIndex = 0; SourceIndex < Source.Num(); ++SourceIndex)
							if (Source[SourceIndex].Name == SourceName)
								TargetToSource[TargetIndex] = SourceIndex;
						for (int32 Child = TargetIndex + 1; Child < Target.Num(); ++Child)
							if (Target[Child].ParentIndex == TargetIndex && Target[Child].Name == AimName)
								TargetAimChild[TargetIndex] = Child;
					}
			}
			SourceYaw = File.contains("SourceYaw") && File["SourceYaw"].is_number_integer()
				? std::clamp(File["SourceYaw"].get<int32>(), 0, 3) : 0;
			Message = "Mapping loaded.";
		}
	}
	ImGui::EndDisabled();
	if (!Message.empty()) ImGui::TextWrapped("%s", Message.c_str());
}

void FPoseRetargetPanel::OnRender()
{
	if (!BeginPanel())
	{
		ImGui::End();
		return;
	}
	DrawRigPicker(0);
	DrawRigPicker(1);
	if (SourceRig)
	{
		if (ImGui::Button("Refresh source animations"))
		{
			SelectAnimation(-1);
			const FString Path = (std::filesystem::path("Assets") / SourceRig->GetPath()).generic_string();
			const TArray<UAnimSequence*>* Sequences = SourceRig->Skeleton ?
				UAssetManager::LoadFbxAnimSequences(Path, *SourceRig->Skeleton) : nullptr;
			AnimationMessage = !Sequences ? "No skeleton for source rig." :
				Sequences->IsEmpty() ? "No AnimSequence uses this skeleton. Import animations with Target Skeleton." : "Animations listed.";
		}
		if (const TArray<UAnimSequence*>* Sequences = UAssetManager::GetFbxAnimSequences(SourceRig->GetPath()))
		{
			ImGui::SameLine();
			ImGui::Text("%d clips", Sequences->Num());
			if (!Sequences->IsEmpty())
			{
				// 테이크 이름은 모두 "Unreal Take"처럼 같을 수 있어 파일 이름(AS_...)으로 표시한다.
				const FString CurrentLabel = SelectedAnimation >= 0 && SelectedAnimation < Sequences->Num()
					? FbxPath::ToString(FbxPath::FromString((*Sequences)[SelectedAnimation]->GetPath()).stem()) : FString("Select animation");
				const char* Current = CurrentLabel.c_str();
				ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 150.0f));
				if (ImGui::BeginCombo("Source animation", Current))
				{
					if (ImGui::Selectable("None", SelectedAnimation < 0)) SelectAnimation(-1);
					for (int32 ClipIndex = 0; ClipIndex < Sequences->Num(); ++ClipIndex)
					{
						ImGui::PushID(ClipIndex);
						if (ImGui::Selectable(FbxPath::ToString(FbxPath::FromString((*Sequences)[ClipIndex]->GetPath()).stem()).c_str(), SelectedAnimation == ClipIndex))
							SelectAnimation(ClipIndex);
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Current);
				if (SelectedAnimation >= 0 && SelectedAnimation < Sequences->Num())
				{
					const UAnimDataModel* Model = (*Sequences)[SelectedAnimation]->GetDataModel();
					if (ImGui::Button(bAnimationPlaying ? "Pause" : "Play")) bAnimationPlaying = !bAnimationPlaying;
					ImGui::SameLine();
					ImGui::Text("%d frames, %d bones", Model->NumberOfKeys, Model->BoneAnimationTracks.Num());
					if (ImGui::SliderFloat("Time", &AnimationTime, 0.0f, Model->PlayLength, "%.2f s"))
					{
						LastAnimationFrame = -1;
						ApplyAnimationFrame();
					}
				}
			}
		}
		if (!AnimationMessage.empty()) ImGui::TextWrapped("%s", AnimationMessage.c_str());
	}
	if (ImGui::Button("Refresh FBX list")) RefreshFbxFiles();
	ImGui::Checkbox("Show bones", &bShowBones);
	ImGui::SameLine();
	if (ImGui::Checkbox("Show mesh", &bShowMesh))
		for (USkeletalMeshComponent* Component : Components)
			if (Component) Component->SetVisibility(bShowMesh);
	ImGui::SameLine();
	ImGui::Checkbox("Show mapping", &bShowMapping);
	ImGui::SameLine();
	const char* ViewModes[] = { "All", "Source FBX", "Target FBX", "Result" };
	ImGui::SetNextItemWidth(EditorUIScale::Px(150.0f));
	ImGui::Combo("View", &FocusMode, ViewModes, 4);
	const bool bMappingVisible = bShowMapping && SourceRig && TargetMesh;
	const float AvailableHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
	const float SplitterHeight = bMappingVisible ? EditorUIScale::Px(8.0f) : 0.0f;
	const float MappingHeight = bMappingVisible ?
		std::min(EditorUIScale::Px(MappingPanelHeight), std::max(EditorUIScale::Px(160.0f), AvailableHeight - EditorUIScale::Px(120.0f) - SplitterHeight)) : 0.0f;
	const float ViewHeight = std::max(1.0f, AvailableHeight - MappingHeight - SplitterHeight -
		(bMappingVisible ? ImGui::GetStyle().ItemSpacing.y * 2.0f : 0.0f));
	const char* ViewLabels[] = { "Source FBX", "Target FBX", "Result" };
	int32 RemainingViews = FocusMode == 0 ? 3 : 1;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!IsViewportVisible(Index)) continue;
		if (RemainingViews < (FocusMode == 0 ? 3 : 1)) ImGui::SameLine();
		ImGui::BeginChild(ViewLabels[Index], ImVec2(ImGui::GetContentRegionAvail().x / RemainingViews, ViewHeight), true);
		DrawViewport(Index, ViewLabels[Index]);
		ImGui::EndChild();
		--RemainingViews;
	}
	if (bMappingVisible)
	{
		ImGui::InvisibleButton("##MappingSplitter", ImVec2(ImGui::GetContentRegionAvail().x, SplitterHeight));
		const ImVec2 SplitMin = ImGui::GetItemRectMin();
		const ImVec2 SplitMax = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(SplitMin.x, (SplitMin.y + SplitMax.y) * 0.5f),
			ImVec2(SplitMax.x, (SplitMin.y + SplitMax.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Separator), EditorUIScale::Px(2.0f));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			// MappingPanelHeight는 100% 배율 기준으로 저장한다.
			MappingPanelHeight = std::clamp(MappingPanelHeight - ImGui::GetIO().MouseDelta.y / EditorUIScale::Px(1.0f),
				160.0f, std::max(160.0f, (AvailableHeight - SplitterHeight) / EditorUIScale::Px(1.0f) - 120.0f));
		ImGui::BeginChild("Mapping controls", ImVec2(0, MappingHeight), true);
		DrawMapping();
		ImGui::EndChild();
	}
	ImGui::End();
}

void FPoseRetargetPanel::DrawBones(int32 Index, FLineBatcher& Lines, const FMatrix& ViewProjection) const
{
	if (!bShowBones || !Components[Index]) return;
	USkeletalMesh* Mesh = Components[Index]->GetSkeletalMesh();
	if (!Mesh) return;
	const TArray<FBoneInfo>& Bones = Mesh->MeshData.Bones;
	const TArray<FMatrix>& Global = Components[Index]->GetGlobalBoneTransforms();
	if (Global.Num() != Bones.Num()) return;
	const FMatrix World = Components[Index]->GetWorldMatrix();
	const float MeshSize = std::max(0.01f, (Mesh->MeshData.AABB.Max - Mesh->MeshData.AABB.Min).Size());
	const float JointRadius = MeshSize * 0.007f;
	const FVector ViewLocation = Viewports[Index].GetViewLocation();
	const FVector4 Color(0.5f, -0.35f, 0.4f, 1.0f);
	Lines.BeginFrame();
	for (int32 Bone = 0; Bone < Bones.Num(); ++Bone)
	{
		const FVector Head = (Global[Bone] * World).TransformPosition(FVector::ZeroVector);
		const FVector Facing = (ViewLocation - Head).GetSafeNormal();
		const FVector Reference = std::abs(Facing.Z) < 0.9f ? FVector(0, 0, 1) : FVector(0, 1, 0);
		const FVector Right = FVector::Cross(Facing, Reference).GetSafeNormal();
		const FVector Up = FVector::Cross(Facing, Right).GetSafeNormal();
		constexpr int32 Segments = 10;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const float A = 2.0f * PI * Segment / Segments;
			const float B = 2.0f * PI * (Segment + 1) / Segments;
			Lines.AddLine(Head + (Right * std::cos(A) + Up * std::sin(A)) * JointRadius,
				Head + (Right * std::cos(B) + Up * std::sin(B)) * JointRadius, Color);
		}
		const int32 Parent = Bones[Bone].ParentIndex;
		if (Parent < 0 || Parent >= Global.Num()) continue;
		const FVector Tail = (Global[Parent] * World).TransformPosition(FVector::ZeroVector);
		const FVector Axis = Head - Tail;
		const float Length = Axis.Size();
		if (Length <= MeshSize * 0.0001f) continue;
		const FVector Direction = Axis / Length;
		const FVector BoneReference = std::abs(Direction.Z) < 0.9f ? FVector(0, 0, 1) : FVector(0, 1, 0);
		const FVector Side = FVector::Cross(Direction, BoneReference).GetSafeNormal();
		const FVector BoneUp = FVector::Cross(Direction, Side).GetSafeNormal();
		const float Width = std::min(Length * 0.13f, MeshSize * 0.018f);
		const FVector Center = Tail + Axis * 0.25f;
		const FVector Ring[4] = { Center + Side * Width, Center + BoneUp * Width,
			Center - Side * Width, Center - BoneUp * Width };
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			Lines.AddLine(Tail, Ring[Edge], Color);
			Lines.AddLine(Ring[Edge], Head, Color);
			Lines.AddLine(Ring[Edge], Ring[(Edge + 1) % 4], Color);
		}
	}
	Lines.OnRender(ViewProjection);
}
TArray<FMatrix> RootCorrections;
