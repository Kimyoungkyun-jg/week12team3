#include "EnginePCH.h"
#include "Editor/Animation/AnimationAssetEditorPanels.h"
#include "Editor/Animation/AnimationEditorSplitter.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/AssetManager.h"
#include "Asset/AnimSequence.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace
{
	constexpr const char* SequencePayload = "ANIMATION_ASSET_SEQUENCE";
	bool InRect(const ImVec2 P, const ImVec2 Min, const ImVec2 Max)
	{ return P.x >= Min.x && P.x <= Max.x && P.y >= Min.y && P.y <= Max.y; }
	FString SequenceLabel(const FString& Path)
	{ return Path.empty() ? "None" : FbxPath::FromString(Path).stem().string(); }
}

FString FAnimationAssetEditorPanel::SkeletonPath() const
{
	return Kind == EKind::Montage ? Montage.SkeletonPath : AimOffset.SkeletonPath;
}

bool FAnimationAssetEditorPanel::IsCompatibleSequence(const FString& Path) const
{
	UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Path);
	return Sequence && Sequence->Skeleton && Sequence->GetDataModel() &&
		(SkeletonPath().empty() || Sequence->Skeleton->GetPath() == SkeletonPath());
}

float FAnimationAssetEditorPanel::SequenceLength() const
{
	UAnimSequence* Sequence = Kind == EKind::Montage ? UAssetManager::LoadAnimSequence(Montage.SequencePath) : nullptr;
	return Sequence && Sequence->GetDataModel() ? std::max(0.0f, Sequence->GetDataModel()->PlayLength) : 0.0f;
}

void FAnimationAssetEditorPanel::NewAsset(const FString& InPath)
{
	Preview.Release();
	Path = InPath;
	Montage = {};
	AimOffset = {};
	SelectedSample = SelectedSection = SelectedNotify = DraggedMarker = -1;
	PreviewTime = PreviewPitch = 0.0f;
	bPlaying = true;
	bDirty = true;
	bFocusRequested = true;
	Status = "Choose a skeleton and animation asset, then save.";
	SetOpen(true);
}

void FAnimationAssetEditorPanel::OpenAsset(const FString& InPath)
{
	if (IsOpen() && Path == InPath) { bFocusRequested = true; return; }
	NewAsset(InPath);
	FString Error;
	const bool Loaded = Kind == EKind::Montage
		? FAnimMontageAsset::Load(InPath, Montage, Error)
		: FAimOffsetAsset::Load(InPath, AimOffset, Error);
	if (!Loaded)
	{
		std::error_code Ec;
		if (fs::exists(FbxPath::FromString(InPath), Ec)) Status = Error;
		return;
	}
	bDirty = false;
	Status.clear();
	StartPreview();
}

void FAnimationAssetEditorPanel::Save()
{
	FString Error;
	if (SkeletonPath().empty()) { Status = "Select a skeleton."; return; }
	if (Kind == EKind::Montage)
	{
		if (!IsCompatibleSequence(Montage.SequencePath)) { Status = "Select a compatible sequence."; return; }
		const float Length = SequenceLength();
		for (const auto& Section : Montage.Sections)
			if (Section.EndTime > Length || Section.StartTime >= Section.EndTime)
			{ Status = "Section bounds must be inside the sequence."; return; }
		for (const auto& Notify : Montage.Notifies)
			if (Notify.Time > Length) { Status = "Notify is outside the sequence."; return; }
		if (!Montage.Save(Path, Error)) { Status = Error; return; }
	}
	else
	{
		if (!IsCompatibleSequence(AimOffset.BaseSequencePath))
		{ Status = "Select a compatible base reference sequence."; return; }
		if (AimOffset.Samples.empty()) { Status = "Add at least one aim sample."; return; }
		std::set<float> UsedPitches;
		for (const auto& Sample : AimOffset.Samples)
		{
			if (!IsCompatibleSequence(Sample.SequencePath) || Sample.Pitch < AimOffset.MinPitch ||
				Sample.Pitch > AimOffset.MaxPitch || !UsedPitches.insert(Sample.Pitch).second)
			{ Status = "Samples need compatible sequences and unique pitches inside the axis."; return; }
		}
		if (!AimOffset.Save(Path, Error)) { Status = Error; return; }
	}
	bDirty = false;
	Status = "Saved " + Path;
	if (OnSaved) OnSaved();
}

void FAnimationAssetEditorPanel::StartPreview()
{
	if (SkeletonPath().empty()) { Preview.Release(); return; }
	FString Error;
	if (!Preview.Start(SkeletonPath(), Error)) Status = Error;
	else EvaluatePreview();
}

void FAnimationAssetEditorPanel::EvaluatePreview()
{
	USkeletalMeshComponent* MeshComponent = Preview.GetMeshComponent();
	USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton) return;
	TArray<FAnimBoneTransform> Pose;
	if (Kind == EKind::Montage)
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Montage.SequencePath);
		if (Sequence && Sequence->Skeleton == Mesh->Skeleton &&
			Sequence->EvaluateLocalPoseTransforms(std::min(PreviewTime, SequenceLength()), Pose))
			Preview.ApplyPose(Pose);
		return;
	}
	UAnimSequence* Base = UAssetManager::LoadAnimSequence(AimOffset.BaseSequencePath);
	if (!Base || Base->Skeleton != Mesh->Skeleton || !Base->GetDataModel()) return;
	FAnimNode_SequencePlayer BasePlayer;
	BasePlayer.Sequence = Base;
	const float FPS = Base->GetDataModel()->FrameRate.AsDecimal();
	BasePlayer.CurrentTime = FPS > 0.0f ? AimOffset.BaseFrameIndex / FPS : 0.0f;
	FAnimNode_AimOffset1D Node;
	Node.BasePose = &BasePlayer;
	Node.Asset = AimOffset;
	Node.BaseSequence = Base;
	Node.Pitch = PreviewPitch;
	for (const auto& Sample : AimOffset.Samples)
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Sample.SequencePath);
		if (Sequence && Sequence->Skeleton == Mesh->Skeleton && Sequence->GetDataModel())
			Node.Samples.Add({ Sample.Pitch, Sequence, Sample.FrameIndex });
	}
	std::sort(Node.Samples.begin(), Node.Samples.end(),
		[](const auto& A, const auto& B) { return A.Pitch < B.Pitch; });
	if (Node.Evaluate(Mesh->Skeleton, Pose)) Preview.ApplyPose(Pose);
}

void FAnimationAssetEditorPanel::Tick(float DeltaTime)
{
	if (!IsOpen()) return;
	if (Kind == EKind::Montage && bPlaying)
	{
		const float Length = SequenceLength();
		if (Length > 0.0f)
		{
			PreviewTime += std::max(0.0f, DeltaTime) * std::max(0.0f, Montage.PlayRate);
			if (PreviewTime > Length)
			{
				if (Montage.bLoop) PreviewTime = std::fmod(PreviewTime, Length);
				else { PreviewTime = Length; bPlaying = false; }
			}
		}
	}
	EvaluatePreview();
	Preview.Tick();
}

void FAnimationAssetEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (IsOpen() && bVisible) Preview.Render(Engine);
}

void FAnimationAssetEditorPanel::DrawAssetDetails()
{
	ImGui::TextUnformatted("Asset Details");
	ImGui::Separator();
	FString& Skeleton = Kind == EKind::Montage ? Montage.SkeletonPath : AimOffset.SkeletonPath;
	if (ImGui::BeginCombo("Skeleton", Skeleton.empty() ? "Select skeleton" : Skeleton.c_str()))
	{
		TArray<UAnimSequence*> Sequences;
		UAssetManager::GetAllAnimSequences(Sequences);
		std::set<FString> Seen;
		for (auto* Seq : Sequences)
		{
			if (!Seq || !Seq->Skeleton) continue;
			const FString Candidate = Seq->Skeleton->GetPath();
			if (!Seen.insert(Candidate).second) continue;
			if (ImGui::Selectable(Candidate.c_str(), Candidate == Skeleton) && Candidate != Skeleton)
			{
				Skeleton = Candidate;
				if (Kind == EKind::Montage) Montage.SequencePath.clear();
				else { AimOffset.BaseSequencePath.clear(); AimOffset.Samples.clear(); SelectedSample = -1; }
				bDirty = true; StartPreview();
			}
		}
		ImGui::EndCombo();
	}
	if (Kind == EKind::Montage)
	{
		ImGui::SeparatorText("Blend Options");
		char Slot[128]{};
		strncpy_s(Slot, Montage.SlotName.c_str(), sizeof(Slot) - 1);
		if (ImGui::InputText("Slot", Slot, sizeof(Slot))) { Montage.SlotName = Slot; bDirty = true; }
		bDirty |= ImGui::DragFloat("Play Rate", &Montage.PlayRate, 0.01f, 0.01f, 10.0f);
		bDirty |= ImGui::DragFloat("Blend In", &Montage.BlendInTime, 0.01f, 0.0f, 5.0f);
		bDirty |= ImGui::DragFloat("Blend Out", &Montage.BlendOutTime, 0.01f, 0.0f, 5.0f);
		bDirty |= ImGui::Checkbox("Loop", &Montage.bLoop);
		ImGui::SeparatorText("Animation");
		ImGui::TextWrapped("Sequence: %s", Montage.SequencePath.empty() ? "Drag a sequence onto the track." : Montage.SequencePath.c_str());
		ImGui::BeginDisabled(SequenceLength() <= 0.0f);
		if (ImGui::Button("Add Section"))
		{
			const float End = SequenceLength();
			Montage.Sections.push_back({ "Section", std::min(PreviewTime, End - 0.001f), End, "" });
			SelectedSection = static_cast<int>(Montage.Sections.size()) - 1; SelectedNotify = -1; bDirty = true;
		}
		if (ImGui::Button("Add Notify"))
		{
			Montage.Notifies.push_back({ "Notify", PreviewTime });
			SelectedNotify = static_cast<int>(Montage.Notifies.size()) - 1; SelectedSection = -1; bDirty = true;
		}
		ImGui::EndDisabled();
		for (int I = 0; I < static_cast<int>(Montage.Sections.size()); ++I)
		{
			ImGui::PushID(I);
			if (ImGui::Selectable(Montage.Sections[I].Name.c_str(), SelectedSection == I))
			{ SelectedSection = I; SelectedNotify = -1; }
			ImGui::PopID();
		}
		return;
	}
	ImGui::SeparatorText("Pitch Axis");
	bDirty |= ImGui::DragFloat("Minimum", &AimOffset.MinPitch, 1.0f, -180.0f, 180.0f);
	bDirty |= ImGui::DragFloat("Maximum", &AimOffset.MaxPitch, 1.0f, -180.0f, 180.0f);
	ImGui::SeparatorText("Additive Settings");
	ImGui::TextWrapped("Base Pose: %s", AimOffset.BaseSequencePath.empty() ? "Drag a sequence here." : AimOffset.BaseSequencePath.c_str());
	bDirty |= ImGui::DragInt("Base Frame", &AimOffset.BaseFrameIndex, 1.0f, 0, 10000);
	ImGui::Button("Drop Base Reference Pose", { -1, EditorUIScale::Px(28) });
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(SequencePayload))
		{
			const FString Candidate = static_cast<const char*>(Payload->Data);
			if (IsCompatibleSequence(Candidate))
			{
				AimOffset.BaseSequencePath = Candidate;
				if (auto* Seq = UAssetManager::LoadAnimSequence(Candidate)) AimOffset.SkeletonPath = Seq->Skeleton->GetPath();
				bDirty = true; StartPreview();
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::Text("Samples (%d)", static_cast<int>(AimOffset.Samples.size()));
	for (int I = 0; I < static_cast<int>(AimOffset.Samples.size()); ++I)
	{
		ImGui::PushID(I);
		const FString Label = SequenceLabel(AimOffset.Samples[I].SequencePath) + "##AimSample";
		if (ImGui::Selectable(Label.c_str(), SelectedSample == I)) SelectedSample = I;
		ImGui::PopID();
	}
}

void FAnimationAssetEditorPanel::DrawGraph()
{
	const ImVec2 Space = ImGui::GetContentRegionAvail();
	const ImVec2 Size(std::max(240.0f, Space.x), std::max(170.0f, Space.y));
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##AnimationAssetGraph", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool Hovered = ImGui::IsItemHovered();
	const bool Active = ImGui::IsItemActive();
	const ImVec2 Mouse = ImGui::GetMousePos();
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	Draw->AddRectFilled(Origin, { Origin.x + Size.x, Origin.y + Size.y }, IM_COL32(27, 29, 34, 255));
	if (Kind == EKind::AimOffset)
	{
		if (AimOffset.MinPitch >= AimOffset.MaxPitch)
		{
			Draw->AddText({ Origin.x + 12.0f, Origin.y + 12.0f }, IM_COL32_WHITE,
				"Set Minimum below Maximum in Asset Details.");
			return;
		}
		const ImVec2 Min(Origin.x + 45.0f, Origin.y + 16.0f);
		const ImVec2 Max(Origin.x + Size.x - 20.0f, Origin.y + Size.y - 23.0f);
		const float Span = std::max(0.01f, AimOffset.MaxPitch - AimOffset.MinPitch);
		const auto YAt = [&](float Pitch)
		{ return Max.y - std::clamp((Pitch - AimOffset.MinPitch) / Span, 0.0f, 1.0f) * (Max.y - Min.y); };
		const auto PitchAt = [&](float Y)
		{ return std::clamp(AimOffset.MinPitch + (Max.y - Y) / (Max.y - Min.y) * Span, AimOffset.MinPitch, AimOffset.MaxPitch); };
		Draw->AddRect(Min, Max, IM_COL32(86, 91, 101, 255));
		const float CX = (Min.x + Max.x) * 0.5f;
		Draw->AddLine({ CX, Min.y }, { CX, Max.y }, IM_COL32(90, 96, 106, 255));
		Draw->AddText({ Origin.x + 3.0f, Min.y }, IM_COL32_WHITE, std::to_string(AimOffset.MaxPitch).c_str());
		Draw->AddText({ Origin.x + 3.0f, Max.y - 12.0f }, IM_COL32_WHITE, std::to_string(AimOffset.MinPitch).c_str());
		for (int I = 0; I < static_cast<int>(AimOffset.Samples.size()); ++I)
		{
			const ImVec2 P(CX, YAt(AimOffset.Samples[I].Pitch));
			Draw->AddCircleFilled(P, I == SelectedSample ? 8.0f : 6.0f,
				I == SelectedSample ? IM_COL32(255, 196, 85, 255) : IM_COL32(215, 220, 230, 255));
			Draw->AddText({ P.x + 12.0f, P.y - 8.0f }, IM_COL32_WHITE,
				SequenceLabel(AimOffset.Samples[I].SequencePath).c_str());
		}
		const ImVec2 PreviewPoint(CX, YAt(PreviewPitch));
		Draw->AddCircle(PreviewPoint, 10.0f, IM_COL32(90, 235, 110, 255), 0, 2.0f);
		Draw->AddLine({ CX - 14.0f, PreviewPoint.y }, { CX + 14.0f, PreviewPoint.y }, IM_COL32(90, 235, 110, 255));
		if (Hovered && ImGui::GetIO().KeyCtrl && InRect(Mouse, Min, Max)) PreviewPitch = PitchAt(Mouse.y);
		if (Hovered && !ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectedSample = -1;
			for (int I = static_cast<int>(AimOffset.Samples.size()) - 1; I >= 0; --I)
				if (std::abs(Mouse.x - CX) < 16.0f && std::abs(Mouse.y - YAt(AimOffset.Samples[I].Pitch)) < 14.0f)
				{ SelectedSample = I; break; }
		}
		if (Active && !ImGui::GetIO().KeyCtrl && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
			SelectedSample >= 0 && SelectedSample < static_cast<int>(AimOffset.Samples.size()))
		{ AimOffset.Samples[SelectedSample].Pitch = PitchAt(Mouse.y); bDirty = true; }
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(SequencePayload))
			{
				const FString Candidate = static_cast<const char*>(Payload->Data);
				if (IsCompatibleSequence(Candidate) && InRect(Mouse, Min, Max))
				{
					if (auto* Seq = UAssetManager::LoadAnimSequence(Candidate))
						if (AimOffset.SkeletonPath.empty()) AimOffset.SkeletonPath = Seq->Skeleton->GetPath();
					AimOffset.Samples.push_back({ PitchAt(Mouse.y), Candidate, 0 });
					SelectedSample = static_cast<int>(AimOffset.Samples.size()) - 1;
					bDirty = true; StartPreview();
				}
			}
			ImGui::EndDragDropTarget();
		}
		return;
	}

	const float Length = std::max(0.01f, SequenceLength());
	const ImVec2 Min(Origin.x + 72.0f, Origin.y + 20.0f);
	const ImVec2 Max(Origin.x + Size.x - 15.0f, Origin.y + Size.y - 15.0f);
	const float Width = Max.x - Min.x;
	const auto XAt = [&](float Time) { return Min.x + std::clamp(Time / Length, 0.0f, 1.0f) * Width; };
	const auto TimeAt = [&](float X) { return std::clamp((X - Min.x) / Width * Length, 0.0f, Length); };
	const float Row = (Max.y - Min.y) / 3.0f;
	const char* Labels[] = { "Sections", "Sequence", "Notifies" };
	for (int I = 0; I < 3; ++I)
	{
		const float Y = Min.y + I * Row;
		Draw->AddRectFilled({ Min.x, Y }, { Max.x, Y + Row - 2.0f },
			I == 1 ? IM_COL32(43, 61, 48, 255) : IM_COL32(37, 39, 45, 255));
		Draw->AddText({ Origin.x + 4.0f, Y + 6.0f }, IM_COL32_WHITE, Labels[I]);
	}
	for (int I = 0; I <= 10; ++I)
	{
		const float X = Min.x + Width * I / 10.0f;
		Draw->AddLine({ X, Min.y - 8.0f }, { X, Max.y }, IM_COL32(68, 72, 80, 255));
	}
	if (!Montage.SequencePath.empty()) Draw->AddText({ Min.x + 8.0f, Min.y + Row + 7.0f }, IM_COL32_WHITE,
		SequenceLabel(Montage.SequencePath).c_str());
	for (int I = 0; I < static_cast<int>(Montage.Sections.size()); ++I)
	{
		const auto& Sec = Montage.Sections[I];
		const ImVec2 A(XAt(Sec.StartTime), Min.y + 3.0f);
		const ImVec2 B(std::max(A.x + 4.0f, XAt(Sec.EndTime)), Min.y + Row - 4.0f);
		Draw->AddRectFilled(A, B, I == SelectedSection ? IM_COL32(102, 120, 161, 255) : IM_COL32(70, 83, 112, 255));
		Draw->AddText({ A.x + 5.0f, A.y + 3.0f }, IM_COL32_WHITE, Sec.Name.c_str());
	}
	for (int I = 0; I < static_cast<int>(Montage.Notifies.size()); ++I)
	{
		const float X = XAt(Montage.Notifies[I].Time);
		const float Y = Min.y + Row * 2.5f;
		Draw->AddQuadFilled({ X, Y - 7.0f }, { X + 7.0f, Y }, { X, Y + 7.0f }, { X - 7.0f, Y },
			I == SelectedNotify ? IM_COL32(255, 218, 100, 255) : IM_COL32(231, 169, 72, 255));
		Draw->AddText({ X + 9.0f, Y - 7.0f }, IM_COL32_WHITE, Montage.Notifies[I].Name.c_str());
	}
	Draw->AddLine({ XAt(PreviewTime), Min.y - 9.0f }, { XAt(PreviewTime), Max.y }, IM_COL32(255, 95, 82, 255), 2.0f);
	if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		DraggedMarker = -1;
		if (Mouse.y < Min.y + Row)
		{
			for (int I = 0; I < static_cast<int>(Montage.Sections.size()); ++I)
			{
				const auto& Sec = Montage.Sections[I];
				if (std::abs(Mouse.x - XAt(Sec.StartTime)) < 8.0f) DraggedMarker = I * 2;
				else if (std::abs(Mouse.x - XAt(Sec.EndTime)) < 8.0f) DraggedMarker = I * 2 + 1;
				if (Mouse.x >= XAt(Sec.StartTime) && Mouse.x <= XAt(Sec.EndTime))
				{ SelectedSection = I; SelectedNotify = -1; }
			}
		}
		else if (Mouse.y >= Min.y + Row * 2.0f)
		{
			for (int I = 0; I < static_cast<int>(Montage.Notifies.size()); ++I)
				if (std::abs(Mouse.x - XAt(Montage.Notifies[I].Time)) < 10.0f)
				{ DraggedMarker = 1000 + I; SelectedNotify = I; SelectedSection = -1; break; }
		}
		if (DraggedMarker < 0) { PreviewTime = TimeAt(Mouse.x); bPlaying = false; }
	}
	if (Active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		if (DraggedMarker >= 1000 && static_cast<size_t>(DraggedMarker - 1000) < Montage.Notifies.size())
		{ Montage.Notifies[DraggedMarker - 1000].Time = TimeAt(Mouse.x); bDirty = true; }
		else if (DraggedMarker >= 0 && static_cast<size_t>(DraggedMarker / 2) < Montage.Sections.size())
		{
			auto& Sec = Montage.Sections[DraggedMarker / 2];
			if (DraggedMarker % 2 == 0) Sec.StartTime = std::clamp(TimeAt(Mouse.x), 0.0f, std::max(0.0f, Sec.EndTime - 0.001f));
			else Sec.EndTime = std::max(TimeAt(Mouse.x), Sec.StartTime + 0.001f);
			bDirty = true;
		}
		else { PreviewTime = TimeAt(Mouse.x); bPlaying = false; }
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(SequencePayload))
		{
			const FString Candidate = static_cast<const char*>(Payload->Data);
			if (IsCompatibleSequence(Candidate) && Mouse.y >= Min.y + Row && Mouse.y < Min.y + Row * 2.0f)
			{
				Montage.SequencePath = Candidate;
				if (auto* Seq = UAssetManager::LoadAnimSequence(Candidate)) Montage.SkeletonPath = Seq->Skeleton->GetPath();
				PreviewTime = 0.0f; bDirty = true; StartPreview();
			}
		}
		ImGui::EndDragDropTarget();
	}
}

void FAnimationAssetEditorPanel::DrawSelectedDetails()
{
	ImGui::TextUnformatted(Kind == EKind::Montage ? "Timeline Details" : "Sample Details");
	ImGui::Separator();
	if (Kind == EKind::AimOffset)
	{
		if (SelectedSample < 0 || SelectedSample >= static_cast<int>(AimOffset.Samples.size()))
		{ ImGui::TextWrapped("Select a sample in the graph."); return; }
		auto& Sample = AimOffset.Samples[SelectedSample];
		ImGui::TextWrapped("%s", Sample.SequencePath.c_str());
		bDirty |= AimOffset.MinPitch < AimOffset.MaxPitch
			? ImGui::DragFloat("Pitch", &Sample.Pitch, 0.5f, AimOffset.MinPitch, AimOffset.MaxPitch)
			: ImGui::DragFloat("Pitch", &Sample.Pitch, 0.5f);
		bDirty |= ImGui::DragInt("Frame", &Sample.FrameIndex, 1.0f, 0, 10000);
		if (ImGui::Button("Delete Sample"))
		{ AimOffset.Samples.erase(AimOffset.Samples.begin() + SelectedSample); SelectedSample = -1; bDirty = true; }
		return;
	}
	if (SelectedSection >= 0 && SelectedSection < static_cast<int>(Montage.Sections.size()))
	{
		auto& Sec = Montage.Sections[SelectedSection];
		char Name[128]{}; strncpy_s(Name, Sec.Name.c_str(), sizeof(Name) - 1);
		if (ImGui::InputText("Section", Name, sizeof(Name))) { Sec.Name = Name; bDirty = true; }
		bDirty |= ImGui::DragFloat("Start", &Sec.StartTime, 0.01f, 0.0f, SequenceLength());
		bDirty |= ImGui::DragFloat("End", &Sec.EndTime, 0.01f, 0.0f, SequenceLength());
		char Next[128]{}; strncpy_s(Next, Sec.NextSection.c_str(), sizeof(Next) - 1);
		if (ImGui::InputText("Next Section", Next, sizeof(Next))) { Sec.NextSection = Next; bDirty = true; }
		if (ImGui::Button("Delete Section"))
		{ Montage.Sections.erase(Montage.Sections.begin() + SelectedSection); SelectedSection = -1; bDirty = true; }
	}
	else if (SelectedNotify >= 0 && SelectedNotify < static_cast<int>(Montage.Notifies.size()))
	{
		auto& Notify = Montage.Notifies[SelectedNotify];
		char Name[128]{}; strncpy_s(Name, Notify.Name.c_str(), sizeof(Name) - 1);
		if (ImGui::InputText("Notify", Name, sizeof(Name))) { Notify.Name = Name; bDirty = true; }
		bDirty |= ImGui::DragFloat("Time", &Notify.Time, 0.01f, 0.0f, SequenceLength());
		if (ImGui::Button("Delete Notify"))
		{ Montage.Notifies.erase(Montage.Notifies.begin() + SelectedNotify); SelectedNotify = -1; bDirty = true; }
	}
	else ImGui::TextWrapped("Select a Section or Notify in the timeline.");
}

void FAnimationAssetEditorPanel::DrawAssetBrowser()
{
	ImGui::TextUnformatted("Animation Assets");
	ImGui::TextDisabled("Drag to graph; double-click to inspect sequence.");
	ImGui::InputText("Search", Search, sizeof(Search));
	TArray<UAnimSequence*> Sequences;
	UAssetManager::GetAllAnimSequences(Sequences);
	for (UAnimSequence* Sequence : Sequences)
	{
		if (!Sequence || !Sequence->Skeleton || !Sequence->GetDataModel()) continue;
		if (!SkeletonPath().empty() && Sequence->Skeleton->GetPath() != SkeletonPath()) continue;
		const FString& Candidate = Sequence->GetPath();
		if (Search[0] && Candidate.find(Search) == FString::npos) continue;
		ImGui::PushID(Candidate.c_str());
		ImGui::Selectable(SequenceLabel(Candidate).c_str());
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnOpenSequence)
			OnOpenSequence(Candidate);
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(SequencePayload, Candidate.c_str(), Candidate.size() + 1);
			ImGui::TextUnformatted(Candidate.c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::PopID();
	}
}

void FAnimationAssetEditorPanel::DrawTransport()
{
	if (Kind == EKind::AimOffset)
	{
		if (AimOffset.MinPitch < AimOffset.MaxPitch)
			ImGui::SliderFloat("Preview Pitch", &PreviewPitch, AimOffset.MinPitch, AimOffset.MaxPitch, "%.1f deg");
		return;
	}
	if (ImGui::Button(bPlaying ? "Pause" : "Play"))
	{
		if (!bPlaying && PreviewTime >= SequenceLength()) PreviewTime = 0.0f;
		bPlaying = !bPlaying;
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart")) { PreviewTime = 0.0f; bPlaying = true; }
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-EditorUIScale::Px(70));
	if (ImGui::SliderFloat("Time", &PreviewTime, 0.0f, std::max(0.01f, SequenceLength()), "%.3fs")) bPlaying = false;
}

void FAnimationAssetEditorPanel::OnRender()
{
	if (!IsOpen()) return;
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(1400), EditorUIScale::Px(850)), ImGuiCond_FirstUseEver);
	if (bFocusRequested) { ImGui::SetNextWindowFocus(); bFocusRequested = false; }
	bVisible = BeginPanel();
	if (!bVisible) { ImGui::End(); return; }
	ImGui::TextUnformatted(Path.c_str()); ImGui::SameLine();
	if (ImGui::Button("Save")) Save();
	if (bDirty) { ImGui::SameLine(); ImGui::TextUnformatted("*"); }
	if (!Status.empty()) ImGui::TextWrapped("%s", Status.c_str());
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float Split = EditorUIScale::Px(6.0f);
	const float Left = Available.x * LeftRatio;
	const float Right = Available.x * RightRatio;
	ImGui::BeginChild("Asset Details", { Left, Available.y }, ImGuiChildFlags_Borders);
	DrawAssetDetails(); ImGui::EndChild(); ImGui::SameLine(0, 0);
	LeftRatio = std::clamp(LeftRatio + AnimationEditorSplitter::Draw("##AssetLeftSplit", true, Available.y) /
		std::max(1.0f, Available.x), 0.10f, 0.40f);
	ImGui::SameLine(0, 0);
	ImGui::BeginChild("Center", { std::max(1.0f, Available.x - Left - Right - Split * 2), Available.y });
	const float CenterHeight = ImGui::GetContentRegionAvail().y;
	const float TransportHeight = EditorUIScale::Px(34.0f);
	const float ContentHeight = std::max(1.0f, CenterHeight - TransportHeight - Split - ImGui::GetStyle().ItemSpacing.y * 2);
	const float ViewportHeight = ContentHeight * ViewportRatio;
	ImGui::BeginChild("Viewport", { 0, ViewportHeight }, ImGuiChildFlags_Borders);
	Preview.Draw(); ImGui::EndChild();
	ViewportRatio = std::clamp(ViewportRatio + AnimationEditorSplitter::Draw("##AssetViewSplit", false,
		ImGui::GetContentRegionAvail().x) / ContentHeight, 0.20f, 0.80f);
	ImGui::BeginChild(Kind == EKind::Montage ? "Montage Timeline" : "Aim Offset Samples",
		{ 0, ContentHeight - ViewportHeight }, ImGuiChildFlags_Borders);
	DrawGraph(); ImGui::EndChild();
	DrawTransport();
	ImGui::EndChild(); ImGui::SameLine(0, 0);
	RightRatio = std::clamp(RightRatio - AnimationEditorSplitter::Draw("##AssetRightSplit", true, Available.y) /
		std::max(1.0f, Available.x), 0.10f, 0.40f);
	ImGui::SameLine(0, 0);
	ImGui::BeginChild("Right", { 0, Available.y });
	const float RightHeight = ImGui::GetContentRegionAvail().y;
	ImGui::BeginChild("Selection Details", { 0, RightHeight * DetailsRatio }, ImGuiChildFlags_Borders);
	DrawSelectedDetails(); ImGui::EndChild();
	DetailsRatio = std::clamp(DetailsRatio + AnimationEditorSplitter::Draw("##AssetDetailsSplit", false,
		ImGui::GetContentRegionAvail().x) / std::max(1.0f, RightHeight), 0.20f, 0.80f);
	ImGui::BeginChild("Asset Browser", { 0, 0 }, ImGuiChildFlags_Borders);
	DrawAssetBrowser(); ImGui::EndChild();
	ImGui::EndChild();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
		ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		if (Kind == EKind::AimOffset && SelectedSample >= 0 && SelectedSample < static_cast<int>(AimOffset.Samples.size()))
		{ AimOffset.Samples.erase(AimOffset.Samples.begin() + SelectedSample); SelectedSample = -1; bDirty = true; }
		else if (Kind == EKind::Montage && SelectedNotify >= 0 && SelectedNotify < static_cast<int>(Montage.Notifies.size()))
		{ Montage.Notifies.erase(Montage.Notifies.begin() + SelectedNotify); SelectedNotify = -1; bDirty = true; }
		else if (Kind == EKind::Montage && SelectedSection >= 0 && SelectedSection < static_cast<int>(Montage.Sections.size()))
		{ Montage.Sections.erase(Montage.Sections.begin() + SelectedSection); SelectedSection = -1; bDirty = true; }
	}
	ImGui::End();
}
