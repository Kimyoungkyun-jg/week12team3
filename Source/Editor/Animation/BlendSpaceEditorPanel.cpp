#include "EnginePCH.h"
#include "Editor/Animation/BlendSpaceEditorPanel.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/Animation/AnimationEditorSplitter.h"
#include "Asset/AssetManager.h"
#include "Asset/AnimSequence.h"
#include "Asset/BlendSpace1D.h"
#include "Asset/BlendSpace2D.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Core/ResourcePaths.h"
#include "Animation/Skeleton.h"
#include "Animation/BoneTransform.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/BillboardComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/Texture2D.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

namespace
{
	constexpr const char* SequencePayload = "BLENDSPACE_SEQUENCE";
	bool InRect(ImVec2 Point, ImVec2 Min, ImVec2 Max)
	{
		return Point.x >= Min.x && Point.x <= Max.x && Point.y >= Min.y && Point.y <= Max.y;
	}
}

void FBlendSpaceEditorPanel::NewAsset(const FString& InPath, bool bIn2D)
{
	Release();
	Path = InPath; SkeletonPath.clear(); Samples.clear(); SelectedSample = -1;
	b2D = bIn2D; bDirty = true; bPlaying = true; PreviewTime = 0.0f;
	XAxis = {}; YAxis = {};
	strcpy_s(XAxis.Name, b2D ? "Direction" : "Speed");
	if (b2D) { XAxis.Min = -180.0f; XAxis.Max = 180.0f; XAxis.Divisions = 8; }
	strcpy_s(YAxis.Name, "Speed");
	PreviewX = std::clamp(0.0f, XAxis.Min, XAxis.Max); PreviewY = 0.0f;
	Status = "Select a skeleton and add animation samples.";
	SetOpen(true);
}

void FBlendSpaceEditorPanel::OpenAsset(const FString& InPath)
{
	if (IsOpen() && Path == InPath) { bFocusRequested = true; return; }
	bFocusRequested = true;
	std::ifstream In(FbxPath::FromString(InPath));
	const json Data = In ? json::parse(In, nullptr, false) : json{};
	if (!Data.is_object() || !Data.contains("Type") || !Data["Type"].is_string() ||
		!Data.contains("Skeleton") || !Data["Skeleton"].is_string() ||
		!Data.contains("Samples") || !Data["Samples"].is_array())
	{ Release(); Status = "Invalid BlendSpace file."; SetOpen(true); return; }
	const FString Type = Data["Type"].get<FString>();
	if (Type != "BlendSpace1D" && Type != "BlendSpace2D")
	{ Release(); Status = "Unknown BlendSpace type."; SetOpen(true); return; }
	NewAsset(InPath, Type == "BlendSpace2D");
	SkeletonPath = Data["Skeleton"].get<FString>();
	for (const auto& Item : Data["Samples"])
	{
		if (!Item.is_object() || !Item.contains("X") || !Item["X"].is_number() ||
			!Item.contains("Sequence") || !Item["Sequence"].is_string() ||
			(b2D && (!Item.contains("Y") || !Item["Y"].is_number())))
		{ Status = "Invalid BlendSpace sample."; return; }
		Samples.push_back({ Item["X"].get<float>(), b2D ? Item["Y"].get<float>() : 0.0f,
			Item["Sequence"].get<FString>() });
	}
	const auto ReadAxis = [](const json& Item, FAxis& Axis)
	{
		if (!Item.is_object()) return;
		if (Item.contains("Name") && Item["Name"].is_string())
			strncpy_s(Axis.Name, Item["Name"].get<FString>().c_str(), _TRUNCATE);
		if (Item.contains("Min") && Item["Min"].is_number()) Axis.Min = Item["Min"].get<float>();
		if (Item.contains("Max") && Item["Max"].is_number()) Axis.Max = Item["Max"].get<float>();
		if (Item.contains("Divisions") && Item["Divisions"].is_number_integer()) Axis.Divisions = Item["Divisions"].get<int>();
		if (Item.contains("Snap") && Item["Snap"].is_boolean()) Axis.bSnap = Item["Snap"].get<bool>();
	};
	if (Data.contains("XAxis")) ReadAxis(Data["XAxis"], XAxis);
	else if (!Samples.empty())
	{
		for (const FSample& Sample : Samples) { XAxis.Min = std::min(XAxis.Min, Sample.X); XAxis.Max = std::max(XAxis.Max, Sample.X); }
	}
	if (b2D && Data.contains("YAxis")) ReadAxis(Data["YAxis"], YAxis);
	else if (b2D && !Samples.empty())
	{
		for (const FSample& Sample : Samples) { YAxis.Min = std::min(YAxis.Min, Sample.Y); YAxis.Max = std::max(YAxis.Max, Sample.Y); }
	}
	if (!std::isfinite(XAxis.Min) || !std::isfinite(XAxis.Max) || XAxis.Min >= XAxis.Max ||
		(b2D && (!std::isfinite(YAxis.Min) || !std::isfinite(YAxis.Max) || YAxis.Min >= YAxis.Max)))
	{ Status = "Invalid BlendSpace axis range."; return; }
	PreviewX = std::clamp(0.0f, XAxis.Min, XAxis.Max);
	PreviewY = std::clamp(0.0f, YAxis.Min, YAxis.Max);
	bDirty = false; Status.clear();
	StartPreview();
}

void FBlendSpaceEditorPanel::Release()
{
	Client.SetWorld(nullptr);
	MeshComponent = nullptr;
	delete PreviewSpace; PreviewSpace = nullptr;
	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld();
		PreviewWorld->FlushPendingDestruction();
		if (GEngine) GEngine->DestroyWorld(PreviewWorld);
		PreviewWorld = nullptr;
	}
}

void FBlendSpaceEditorPanel::StartPreview()
{
	Release();
	if (SkeletonPath.empty()) return;
	USkeleton* Skeleton = UAssetManager::LoadSkeleton(SkeletonPath);
	if (!Skeleton) { Status = "Cannot load skeleton."; return; }
	TArray<USkeletalMesh*> Meshes;
	UAssetManager::GetSkeletalMeshesForSkeleton(Skeleton, Meshes);
	if (Meshes.IsEmpty())
	{
		fs::path SkeletonFile;
		FString Error;
		if (FbxPath::ResolveKey(SkeletonPath, SkeletonFile, Error))
		{
			std::error_code Ec;
			for (fs::directory_iterator It(SkeletonFile.parent_path(), Ec), End; !Ec && It != End; It.increment(Ec))
			{
				if (!It->is_regular_file() || It->path().extension() != ".fbxbin" ||
					!It->path().filename().string().starts_with("SK_")) continue;
				UAssetManager::LoadFbxSkeletalMesh(FbxPath::ToString(It->path()));
				UAssetManager::GetSkeletalMeshesForSkeleton(Skeleton, Meshes);
				if (!Meshes.IsEmpty()) break;
			}
		}
	}
	if (Meshes.IsEmpty()) { Status = "No skeletal mesh for the selected skeleton."; return; }
	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!PreviewWorld) return;
	AActor* Actor = PreviewWorld->SpawnActor<AActor>();
	if (!Actor) { Release(); return; }
	MeshComponent = Cast<USkeletalMeshComponent>(Actor->AddComponent(USkeletalMeshComponent::StaticClass(), "BlendSpacePreview"));
	if (!MeshComponent) { Release(); return; }
	Actor->SetRootComponent(MeshComponent);
	MeshComponent->SetSkeletalMesh(Meshes[0]);
	if (auto* Sky = PreviewWorld->SpawnActor<ASkyLightActor>())
	{ Sky->GetBillboardComponent()->SetVisibility(false); Sky->GetLightComponent()->SetIntensity(0.35f); }
	if (auto* Sun = PreviewWorld->SpawnActor<ADirectionalLightActor>())
	{
		Sun->GetBillboardComponent()->SetVisibility(false);
		Sun->GetArrowComponent()->SetVisibility(false);
		Sun->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
		Sun->GetLightComponent()->SetIntensity(2.0f);
	}
	Client.SetWorld(PreviewWorld);
	Client.SetViewportType(ELevelViewportType::Perspective);
	Client.Resize(800, 600);
	const FBox Bounds = MeshComponent->CalcBounds();
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = std::max(0.5f, (Bounds.Max - Bounds.Min).Size() * 0.5f);
	Client.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	Client.SetViewLocation(Center - Client.GetViewForward() * (Radius * 2.5f));
	RebuildBlendSpace();
}

bool FBlendSpaceEditorPanel::RebuildBlendSpace()
{
	delete PreviewSpace; PreviewSpace = nullptr;
	if (SkeletonPath.empty() || Samples.empty()) return false;
	USkeleton* Skeleton = UAssetManager::LoadSkeleton(SkeletonPath);
	if (!Skeleton) return false;
	PreviewSpace = b2D ? static_cast<UBlendSpace*>(NewObject<UBlendSpace2D>()) :
		static_cast<UBlendSpace*>(NewObject<UBlendSpace1D>());
	PreviewSpace->Skeleton = Skeleton;
	for (const FSample& Sample : Samples)
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Sample.SequencePath);
		const bool Added = b2D ? static_cast<UBlendSpace2D*>(PreviewSpace)->AddSample(Sample.X, Sample.Y, Sequence) :
			static_cast<UBlendSpace1D*>(PreviewSpace)->AddSample(Sample.X, Sequence);
		if (!Added)
		{
			Status = "Sample is missing, incompatible, or shares another sample position.";
			delete PreviewSpace; PreviewSpace = nullptr;
			return false;
		}
	}
	if (Status.starts_with("Sample is")) Status.clear();
	EvaluatePreview();
	return true;
}

void FBlendSpaceEditorPanel::EvaluatePreview()
{
	if (!PreviewSpace || !MeshComponent) return;
	TArray<FAnimBoneTransform> Pose;
	const float Length = PreviewSpace->GetEffectiveLength(PreviewX, PreviewY);
	const float Time = Length > 0.0f ? std::fmod(std::max(0.0f, PreviewTime) / Length, 1.0f) : 0.0f;
	if (!PreviewSpace->Evaluate(PreviewX, PreviewY, Time, Pose)) return;
	for (int32 Index = 0; Index < Pose.Num(); ++Index)
		MeshComponent->SetBoneTransform(Index, MakeAnimMatrix(Pose[Index].Position, Pose[Index].Rotation, Pose[Index].Scale));
}

void FBlendSpaceEditorPanel::Tick(float DeltaTime)
{
	if (!IsOpen())
	{
		bFocused = false;
		// 창을 완전히 닫으면 프리뷰 월드를 해제한다. 샘플 데이터는 유지하고, 다시 열면 OnRender에서 프리뷰를 복원한다.
		if (PreviewWorld)
		{
			Release();
			bRestorePreviewOnOpen = true;
		}
		return;
	}
	// 도킹된 탭이 가려져 있으면 재생하지 않는다. 월드는 그대로 둔다.
	if (!bVisible)
	{
		return;
	}
	if (!PreviewWorld || !MeshComponent) return;
	if (bPlaying) PreviewTime += std::max(0.0f, DeltaTime);
	EvaluatePreview();
	PreviewWorld->Tick(0.0f);
	MeshComponent->TickComponent(0.0f);
}

void FBlendSpaceEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (Engine && IsOpen() && bVisible && PreviewWorld)
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, nullptr, true);
}

float FBlendSpaceEditorPanel::Snap(float Value, const FAxis& Axis) const
{
	Value = std::clamp(Value, Axis.Min, Axis.Max);
	if (Axis.bSnap && Axis.Divisions > 0)
	{
		const float Step = (Axis.Max - Axis.Min) / Axis.Divisions;
		if (Step > 0.0f) Value = Axis.Min + std::round((Value - Axis.Min) / Step) * Step;
	}
	return std::clamp(Value, Axis.Min, Axis.Max);
}

bool FBlendSpaceEditorPanel::IsSamplePositionTaken(int Ignore, float X, float Y) const
{
	for (int I = 0; I < static_cast<int>(Samples.size()); ++I)
		if (I != Ignore && Samples[I].X == X && (!b2D || Samples[I].Y == Y)) return true;
	return false;
}

bool FBlendSpaceEditorPanel::Validate(FString& Error) const
{
	if (Path.empty() || FbxPath::FromString(Path).extension() != ".blendspace" || SkeletonPath.empty())
	{ Error = "Select a skeleton and a BlendSpace path."; return false; }
	if (!std::isfinite(XAxis.Min) || !std::isfinite(XAxis.Max) || XAxis.Min >= XAxis.Max ||
		(b2D && (!std::isfinite(YAxis.Min) || !std::isfinite(YAxis.Max) || YAxis.Min >= YAxis.Max)) ||
		XAxis.Divisions < 1 || XAxis.Divisions > 64 || (b2D && (YAxis.Divisions < 1 || YAxis.Divisions > 64)))
	{ Error = "Axis ranges and grid divisions are invalid."; return false; }
	if (Samples.empty() || Samples.size() > 256) { Error = "Add 1 to 256 animation samples."; return false; }
	for (size_t I = 0; I < Samples.size(); ++I)
	{
		const FSample& Sample = Samples[I];
		if (!std::isfinite(Sample.X) || Sample.X < XAxis.Min || Sample.X > XAxis.Max ||
			(b2D && (!std::isfinite(Sample.Y) || Sample.Y < YAxis.Min || Sample.Y > YAxis.Max)))
		{ Error = "A sample is outside the axis range."; return false; }
		for (size_t J = 0; J < I; ++J)
			if (Sample.X == Samples[J].X && (!b2D || Sample.Y == Samples[J].Y))
			{ Error = "Two samples occupy the same position."; return false; }
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Sample.SequencePath);
		if (!Sequence || !Sequence->Skeleton || Sequence->Skeleton->GetPath() != SkeletonPath)
		{ Error = "A sample has an incompatible animation."; return false; }
	}
	Error.clear();
	return true;
}

void FBlendSpaceEditorPanel::Save()
{
	if (!Validate(Status)) return;
	if (!PreviewSpace && !RebuildBlendSpace()) return;
	FString Key;
	if (!FbxPath::GetAssetKey(Path, Key, Status, false) || !Key.ends_with(".blendspace"))
	{ Status = "BlendSpace path must be inside Assets."; return; }
	json Data = { { "Type", b2D ? "BlendSpace2D" : "BlendSpace1D" }, { "Skeleton", SkeletonPath },
		{ "XAxis", { { "Name", XAxis.Name }, { "Min", XAxis.Min }, { "Max", XAxis.Max },
			{ "Divisions", XAxis.Divisions }, { "Snap", XAxis.bSnap } } }, { "Samples", json::array() } };
	if (b2D) Data["YAxis"] = { { "Name", YAxis.Name }, { "Min", YAxis.Min }, { "Max", YAxis.Max },
		{ "Divisions", YAxis.Divisions }, { "Snap", YAxis.bSnap } };
	for (const FSample& Sample : Samples)
	{
		json Item = { { "X", Sample.X }, { "Sequence", Sample.SequencePath } };
		if (b2D) Item["Y"] = Sample.Y;
		Data["Samples"].push_back(std::move(Item));
	}
	const fs::path File = fs::path(ContentPaths::AssetRoot) / FbxPath::FromString(Key);
	const fs::path Temp = File.string() + ".editing";
	std::ofstream Out(Temp, std::ios::trunc);
	if (!Out) { Status = "Cannot write BlendSpace."; return; }
	Out << Data.dump(2); Out.close();
	if (!Out) { Status = "Cannot finish writing BlendSpace."; return; }
	if (!MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{ Status = "Cannot replace BlendSpace."; std::error_code Ec; fs::remove(Temp, Ec); return; }
	bool Updated = false;
	if (UBlendSpace* Cached = UAssetManager::GetAssetByPath<UBlendSpace>(Path))
	{
		if (b2D && Cached->IsA<UBlendSpace2D>())
			Updated = static_cast<UBlendSpace2D*>(Cached)->ReplaceSamples(static_cast<UBlendSpace2D*>(PreviewSpace)->GetSamples());
		else if (!b2D && Cached->IsA<UBlendSpace1D>())
			Updated = static_cast<UBlendSpace1D*>(Cached)->ReplaceSamples(static_cast<UBlendSpace1D*>(PreviewSpace)->GetSamples());
	}
	else Updated = UAssetManager::LoadBlendSpace(Path) != nullptr;
	if (!Updated) { Status = "Saved file, but the loaded BlendSpace could not refresh."; return; }
	bDirty = false; Status = "Saved";
}

void FBlendSpaceEditorPanel::AddSample(const FString& SequencePath, float X, float Y)
{
	UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(SequencePath);
	if (!Sequence || !Sequence->Skeleton || Sequence->Skeleton->GetPath() != SkeletonPath)
	{ Status = "Animation does not match the selected skeleton."; return; }
	X = Snap(X, XAxis); Y = b2D ? Snap(Y, YAxis) : 0.0f;
	if (IsSamplePositionTaken(-1, X, Y)) { Status = "A sample already occupies this position."; return; }
	Samples.push_back({ X, Y, SequencePath });
	SelectedSample = static_cast<int>(Samples.size()) - 1;
	bDirty = true; RebuildBlendSpace();
}

void FBlendSpaceEditorPanel::DrawAssetDetails()
{
	ImGui::TextUnformatted("Asset Details");
	ImGui::Separator();
	if (ImGui::BeginCombo("Skeleton", SkeletonPath.empty() ? "Select skeleton" : SkeletonPath.c_str()))
	{
		TArray<UAnimSequence*> Sequences;
		UAssetManager::GetAllAnimSequences(Sequences);
		std::set<FString> Seen;
		for (UAnimSequence* Sequence : Sequences)
		{
			if (!Sequence || !Sequence->Skeleton) continue;
			const FString Candidate = Sequence->Skeleton->GetPath();
			if (!Seen.insert(Candidate).second) continue;
			if (ImGui::Selectable(Candidate.c_str(), Candidate == SkeletonPath) && Candidate != SkeletonPath)
			{ SkeletonPath = Candidate; Samples.clear(); SelectedSample = -1; bDirty = true; StartPreview(); }
		}
		ImGui::EndCombo();
	}
	const auto AxisEditor = [this](const char* Label, FAxis& Axis)
	{
		if (!ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen)) return;
		bDirty |= ImGui::InputText("Name", Axis.Name, sizeof(Axis.Name));
		bDirty |= ImGui::DragFloat("Minimum", &Axis.Min, 1.0f);
		bDirty |= ImGui::DragFloat("Maximum", &Axis.Max, 1.0f);
		bDirty |= ImGui::DragInt("Grid Divisions", &Axis.Divisions, 1.0f, 1, 64);
		bDirty |= ImGui::Checkbox("Snap Samples", &Axis.bSnap);
		ImGui::TreePop();
	};
	ImGui::PushID("XAxis"); AxisEditor("Horizontal Axis", XAxis); ImGui::PopID();
	if (b2D) { ImGui::PushID("YAxis"); AxisEditor("Vertical Axis", YAxis); ImGui::PopID(); }
	if (XAxis.Min < XAxis.Max) PreviewX = std::clamp(PreviewX, XAxis.Min, XAxis.Max);
	if (b2D && YAxis.Min < YAxis.Max) PreviewY = std::clamp(PreviewY, YAxis.Min, YAxis.Max);
	ImGui::Separator();
	ImGui::Text("Samples (%d)", static_cast<int>(Samples.size()));
	for (int I = 0; I < static_cast<int>(Samples.size()); ++I)
	{
		ImGui::PushID(I);
		const FString Label = FbxPath::FileName(Samples[I].SequencePath) + "##Sample";
		if (ImGui::Selectable(Label.c_str(), SelectedSample == I)) SelectedSample = I;
		ImGui::PopID();
	}
}

void FBlendSpaceEditorPanel::DrawGraph()
{
	ImGui::Text("Blend Graph    Ctrl + mouse: preview %s%s", XAxis.Name, b2D ? " / vertical axis" : "");
	if (XAxis.Min >= XAxis.Max || (b2D && YAxis.Min >= YAxis.Max))
	{ ImGui::TextUnformatted("Set a valid axis range."); return; }
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const ImVec2 Size(std::max(180.0f, Available.x), std::max(160.0f, Available.y));
	const ImVec2 Outer = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##BlendGraph", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool Hovered = ImGui::IsItemHovered();
	const ImVec2 Mouse = ImGui::GetMousePos();
	const ImVec2 Min(Outer.x + 48.0f, Outer.y + 20.0f);
	const ImVec2 Max(Outer.x + Size.x - 18.0f, Outer.y + Size.y - 32.0f);
	const float Width = Max.x - Min.x, Height = Max.y - Min.y;
	const auto ToScreen = [&](float X, float Y)
	{
		return ImVec2(Min.x + (X - XAxis.Min) / (XAxis.Max - XAxis.Min) * Width,
			b2D ? Max.y - (Y - YAxis.Min) / (YAxis.Max - YAxis.Min) * Height : (Min.y + Max.y) * 0.5f);
	};
	const auto FromScreen = [&](ImVec2 Point)
	{
		return ImVec2(std::clamp(XAxis.Min + (Point.x - Min.x) / Width * (XAxis.Max - XAxis.Min), XAxis.Min, XAxis.Max),
			b2D ? std::clamp(YAxis.Min + (Max.y - Point.y) / Height * (YAxis.Max - YAxis.Min), YAxis.Min, YAxis.Max) : 0.0f);
	};
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	Draw->AddRectFilled(Min, Max, IM_COL32(26, 29, 34, 255));
	Draw->AddRect(Min, Max, IM_COL32(100, 105, 115, 255));
	for (int I = 0; I <= std::clamp(XAxis.Divisions, 1, 64); ++I)
	{
		const float X = Min.x + Width * I / std::clamp(XAxis.Divisions, 1, 64);
		Draw->AddLine({ X, Min.y }, { X, Max.y }, IM_COL32(65, 69, 77, 255));
	}
	if (b2D)
		for (int I = 0; I <= std::clamp(YAxis.Divisions, 1, 64); ++I)
		{
			const float Y = Min.y + Height * I / std::clamp(YAxis.Divisions, 1, 64);
			Draw->AddLine({ Min.x, Y }, { Max.x, Y }, IM_COL32(65, 69, 77, 255));
		}
	else Draw->AddLine({ Min.x, (Min.y + Max.y) * 0.5f }, { Max.x, (Min.y + Max.y) * 0.5f }, IM_COL32(150, 155, 165, 255), 2.0f);
	if (b2D && PreviewSpace && PreviewSpace->IsA<UBlendSpace2D>())
	{
		for (const auto& Triangle : static_cast<UBlendSpace2D*>(PreviewSpace)->GetTriangles())
		{
			const ImVec2 A = ToScreen(Samples[Triangle.A].X, Samples[Triangle.A].Y);
			const ImVec2 B = ToScreen(Samples[Triangle.B].X, Samples[Triangle.B].Y);
			const ImVec2 C = ToScreen(Samples[Triangle.C].X, Samples[Triangle.C].Y);
			Draw->AddLine(A, B, IM_COL32(135, 140, 150, 255));
			Draw->AddLine(B, C, IM_COL32(135, 140, 150, 255));
			Draw->AddLine(C, A, IM_COL32(135, 140, 150, 255));
		}
	}
	Draw->AddText({ Min.x, Max.y + 5 }, IM_COL32_WHITE, std::to_string(XAxis.Min).c_str());
	Draw->AddText({ Max.x - 45, Max.y + 5 }, IM_COL32_WHITE, std::to_string(XAxis.Max).c_str());
	Draw->AddText({ (Min.x + Max.x) * 0.5f - 30, Max.y + 5 }, IM_COL32_WHITE, XAxis.Name);
	if (b2D)
	{
		Draw->AddText({ Outer.x, Min.y }, IM_COL32_WHITE, std::to_string(YAxis.Max).c_str());
		Draw->AddText({ Outer.x, Max.y - 15 }, IM_COL32_WHITE, std::to_string(YAxis.Min).c_str());
		Draw->AddText({ Outer.x, (Min.y + Max.y) * 0.5f }, IM_COL32_WHITE, YAxis.Name);
	}
	for (int I = 0; I < static_cast<int>(Samples.size()); ++I)
	{
		const ImVec2 Point = ToScreen(Samples[I].X, Samples[I].Y);
		const bool Outside = Samples[I].X < XAxis.Min || Samples[I].X > XAxis.Max ||
			(b2D && (Samples[I].Y < YAxis.Min || Samples[I].Y > YAxis.Max));
		Draw->AddCircleFilled(Point, I == SelectedSample ? 7.0f : 5.0f,
			Outside ? IM_COL32(255, 90, 90, 255) :
			I == SelectedSample ? IM_COL32(255, 190, 90, 255) : IM_COL32(225, 225, 230, 255));
	}
	if (Hovered && ImGui::GetIO().KeyCtrl && InRect(Mouse, Min, Max))
	{
		const ImVec2 Value = FromScreen(Mouse);
		PreviewX = Value.x; PreviewY = Value.y;
		ImGui::SetTooltip("%s %.2f%s", XAxis.Name, PreviewX,
			b2D ? (FString("  ") + YAxis.Name + " " + std::to_string(PreviewY)).c_str() : "");
	}
	else if (Hovered && !ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		SelectedSample = -1;
		for (int I = static_cast<int>(Samples.size()) - 1; I >= 0; --I)
		{
			const ImVec2 Point = ToScreen(Samples[I].X, Samples[I].Y);
			if (std::abs(Mouse.x - Point.x) <= 10.0f && std::abs(Mouse.y - Point.y) <= 10.0f)
			{ SelectedSample = I; break; }
		}
	}
	if (Hovered && !ImGui::GetIO().KeyCtrl && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
		SelectedSample >= 0 && SelectedSample < static_cast<int>(Samples.size()))
	{
		const ImVec2 Value = FromScreen(Mouse);
		const float X = Snap(Value.x, XAxis);
		const float Y = b2D ? Snap(Value.y, YAxis) : 0.0f;
		// 다른 샘플이 있는 격자점에는 놓지 않는다. 겹치면 점이 가려지고 AddSample이 거부해 프리뷰가 사라진다.
		// 위치가 바뀐 프레임에만 다시 만든다.
		FSample& Sample = Samples[SelectedSample];
		if ((Sample.X != X || Sample.Y != Y) && !IsSamplePositionTaken(SelectedSample, X, Y))
		{
			Sample.X = X; Sample.Y = Y;
			bDirty = true; RebuildBlendSpace();
		}
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(SequencePayload))
		{
			if (InRect(Mouse, Min, Max))
			{
				const ImVec2 Value = FromScreen(Mouse);
				AddSample(static_cast<const char*>(Payload->Data), Value.x, Value.y);
			}
		}
		ImGui::EndDragDropTarget();
	}
	const ImVec2 Preview = ToScreen(PreviewX, PreviewY);
	Draw->AddCircle(Preview, 9.0f, IM_COL32(90, 235, 110, 255), 0, 2.0f);
	Draw->AddLine({ Preview.x - 12, Preview.y }, { Preview.x + 12, Preview.y }, IM_COL32(90, 235, 110, 255));
	Draw->AddLine({ Preview.x, Preview.y - 12 }, { Preview.x, Preview.y + 12 }, IM_COL32(90, 235, 110, 255));
}

void FBlendSpaceEditorPanel::DrawSampleDetails()
{
	ImGui::TextUnformatted("Sample Details");
	ImGui::Separator();
	if (SelectedSample < 0 || SelectedSample >= static_cast<int>(Samples.size()))
	{ ImGui::TextWrapped("Select a sample in the graph."); return; }
	FSample& Sample = Samples[SelectedSample];
	ImGui::TextWrapped("%s", Sample.SequencePath.c_str());
	float X = Sample.X, Y = Sample.Y;
	bool Changed = ImGui::DragFloat("X", &X, 0.5f, XAxis.Min, XAxis.Max);
	if (b2D) Changed |= ImGui::DragFloat("Y", &Y, 0.5f, YAxis.Min, YAxis.Max);
	if (Changed && !IsSamplePositionTaken(SelectedSample, X, b2D ? Y : 0.0f))
	{
		Sample.X = X; Sample.Y = b2D ? Y : 0.0f;
		bDirty = true; RebuildBlendSpace();
	}
	if (ImGui::Button("Delete Sample"))
	{
		Samples.erase(Samples.begin() + SelectedSample);
		SelectedSample = -1; bDirty = true; RebuildBlendSpace();
	}
}

void FBlendSpaceEditorPanel::DrawAssetBrowser()
{
	ImGui::TextUnformatted("Animation Assets");
	ImGui::TextDisabled("Drag to graph to add; double-click to open sequence.");
	ImGui::InputText("Search", Search, sizeof(Search));
	if (SkeletonPath.empty()) { ImGui::TextWrapped("Select a skeleton first."); return; }
	USkeleton* Skeleton = UAssetManager::LoadSkeleton(SkeletonPath);
	TArray<UAnimSequence*> Sequences;
	UAssetManager::GetAnimSequencesForSkeleton(Skeleton, Sequences);
	for (UAnimSequence* Sequence : Sequences)
	{
		if (!Sequence) continue;
		const FString& Name = Sequence->GetPath();
		if (Search[0] && Name.find(Search) == FString::npos) continue;
		ImGui::PushID(Name.c_str());
		ImGui::Selectable(Name.c_str());
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			if (OnOpenSequence) OnOpenSequence(Name);
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(SequencePayload, Name.c_str(), Name.size() + 1);
			ImGui::TextUnformatted(Name.c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::PopID();
	}
}

void FBlendSpaceEditorPanel::OnRender()
{
	if (!IsOpen()) return;
	if (bRestorePreviewOnOpen)
	{
		bRestorePreviewOnOpen = false;
		StartPreview();
	}
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(1400), EditorUIScale::Px(850)), ImGuiCond_FirstUseEver);
	if (bFocusRequested) { ImGui::SetNextWindowFocus(); bFocusRequested = false; }
	bVisible = BeginPanel();
	bFocused = bVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	// Ctrl+S: 이 창에 포커스가 있을 때만 저장한다. Ctrl+Shift+S는 전체 저장이라 여기서 받지 않는다.
	if (bFocused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		Save();
	}
	if (!bVisible) { ImGui::End(); return; }
	ImGui::TextUnformatted(Path.c_str()); ImGui::SameLine();
	if (ImGui::Button("Save")) Save(); ImGui::SameLine();
	if (bDirty) ImGui::TextUnformatted("*");
	if (!Status.empty()) ImGui::TextWrapped("%s", Status.c_str());
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float Split = EditorUIScale::Px(6);
	const float Left = Available.x * LeftRatio;
	const float Right = Available.x * RightRatio;
	ImGui::BeginChild("Asset Details", { Left, Available.y }, ImGuiChildFlags_Borders);
	DrawAssetDetails(); ImGui::EndChild(); ImGui::SameLine(0, 0);
	LeftRatio = std::clamp(LeftRatio + AnimationEditorSplitter::Draw("##BlendLeftSplit", true, Available.y) /
		std::max(1.0f, Available.x), 0.10f, 0.40f);
	ImGui::SameLine(0, 0);
	ImGui::BeginChild("Center", { std::max(1.0f, Available.x - Left - Right - Split * 2), Available.y });
	const float CenterHeight = ImGui::GetContentRegionAvail().y;
	const float TimelineHeight = EditorUIScale::Px(34);
	const float ContentHeight = std::max(1.0f, CenterHeight - TimelineHeight - Split - ImGui::GetStyle().ItemSpacing.y * 2);
	const float ViewportHeight = ContentHeight * ViewportRatio;
	ImGui::BeginChild("Viewport", { 0, ViewportHeight }, ImGuiChildFlags_Borders);
	if (PreviewWorld && Client.GetColorTarget())
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		Client.Resize(static_cast<uint32>(std::max(1.0f, Size.x)), static_cast<uint32>(std::max(1.0f, Size.y)));
		ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);
		const bool bHovered = ImGui::IsItemHovered();
		const bool bCaptured = bHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
		float Wheel = ImGui::GetIO().MouseWheel;
		// 우클릭으로 카메라를 잡은 동안 휠은 이동 속도를 바꾼다.
		if (bCaptured && Wheel != 0.0f)
		{
			CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
			Wheel = 0.0f;
		}
		Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, bCaptured, bHovered,
			static_cast<int32>(Wheel * 120), 4.0f * CameraSpeed, 0.2f);
	}
	else ImGui::TextWrapped("Select a skeleton with a compatible skeletal mesh.");
	ImGui::EndChild();
	ViewportRatio = std::clamp(ViewportRatio + AnimationEditorSplitter::Draw("##BlendViewSplit", false,
		ImGui::GetContentRegionAvail().x) / ContentHeight, 0.20f, 0.80f);
	ImGui::BeginChild("Blend Graph", { 0, ContentHeight - ViewportHeight }, ImGuiChildFlags_Borders);
	DrawGraph(); ImGui::EndChild();
	const float Length = PreviewSpace ? PreviewSpace->GetEffectiveLength(PreviewX, PreviewY) : 0.0f;
	if (ImGui::Button(bPlaying ? "Pause" : "Play")) bPlaying = !bPlaying;
	ImGui::SameLine();
	if (ImGui::Button("Restart")) PreviewTime = 0.0f;
	ImGui::SameLine();
	float Position = Length > 0.0f ? std::fmod(PreviewTime, Length) : 0.0f;
	ImGui::SetNextItemWidth(-EditorUIScale::Px(75));
	if (ImGui::SliderFloat("Time", &Position, 0.0f, std::max(Length, 0.01f))) PreviewTime = Position;
	ImGui::EndChild(); ImGui::SameLine(0, 0);
	RightRatio = std::clamp(RightRatio - AnimationEditorSplitter::Draw("##BlendRightSplit", true, Available.y) /
		std::max(1.0f, Available.x), 0.10f, 0.40f);
	ImGui::SameLine(0, 0);
	ImGui::BeginChild("Right", { 0, Available.y });
	const float RightHeight = ImGui::GetContentRegionAvail().y;
	ImGui::BeginChild("Sample Details", { 0, RightHeight * DetailsRatio }, ImGuiChildFlags_Borders);
	DrawSampleDetails(); ImGui::EndChild();
	DetailsRatio = std::clamp(DetailsRatio + AnimationEditorSplitter::Draw("##BlendDetailsSplit", false,
		ImGui::GetContentRegionAvail().x) / std::max(1.0f, RightHeight), 0.20f, 0.80f);
	ImGui::BeginChild("Asset Browser", { 0, 0 }, ImGuiChildFlags_Borders);
	DrawAssetBrowser(); ImGui::EndChild();
	ImGui::EndChild();
	if (SelectedSample >= 0 && SelectedSample < static_cast<int>(Samples.size()) &&
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
		ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		Samples.erase(Samples.begin() + SelectedSample);
		SelectedSample = -1; bDirty = true; RebuildBlendSpace();
	}
	ImGui::End();
}
