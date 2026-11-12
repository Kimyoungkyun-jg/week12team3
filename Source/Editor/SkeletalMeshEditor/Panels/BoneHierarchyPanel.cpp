#include "EnginePCH.h"
#include "BoneHierarchyPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"
#include "Runtime/Render/SkeletalMeshData.h"

FBoneHierarchyPanel::FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Bone Hierarchy")
{
}

void FBoneHierarchyPanel::DrawContent()
{
	DrawTree(Editor);
}

void FBoneHierarchyPanel::DrawTree(FSkeletalPreviewEditor& InEditor)
{
	if (!InEditor.GetMesh())
	{
		return;
	}

	if (ImGui::BeginChild("BoneHierarchyChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);

		for (int32 RootBoneIndex : InEditor.GetRootBoneIndices())
		{
			DrawBoneHierarchy(InEditor, RootBoneIndex);
		}

		ImGui::PopStyleVar();
	}

	ImGui::EndChild();
}

void FBoneHierarchyPanel::DrawBoneHierarchy(FSkeletalPreviewEditor& InEditor, int32 BoneIndex)
{
	ImGuiTreeNodeFlags Flag =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	const TArray<FEditorBoneItem>& BoneItems = InEditor.GetBoneItems();

	int32 ChildBoneCount = BoneItems[BoneIndex].ChildBoneIndices.Num();
	if (ChildBoneCount == 0)
	{
		Flag |= ImGuiTreeNodeFlags_Leaf;
	}

	if (InEditor.GetSelectedBoneIndex() == BoneIndex)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::PushID(BoneIndex);

	// 표시 이름과 ID를 분리하여 축약된 이름이 겹쳐도 구분되게 함
	const FString& BoneName = BoneItems[BoneIndex].Name;

	// 접힌 노드도 선택할 수 있도록 펼침 상태와 선택 처리를 분리
	const bool bOpen = ImGui::TreeNodeEx("Bone", Flag, "%s", BoneName.c_str());

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		InEditor.SelectBone(BoneIndex);
	}

	if (bOpen)
	{
		for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
		{
			DrawBoneHierarchy(InEditor, ChildIndex);
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}
