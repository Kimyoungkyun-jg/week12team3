#include "EnginePCH.h"
#include "StaticMeshInfoPanel.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "Render/Mesh.h"

#include <filesystem>

FStaticMeshInfoPanel::FStaticMeshInfoPanel(FStaticMeshEditor& InEditor)
	: FStaticMeshEditorPanel(InEditor, "Info")
{
}

void FStaticMeshInfoPanel::DrawContent()
{
	UStaticMesh* Mesh = Editor.GetMesh();
	if (!Mesh)
	{
		return;
	}

	const FString Name = std::filesystem::path(Editor.GetAssetPath()).stem().string();
	ImGui::TextDisabled("Asset Name");
	ImGui::TextWrapped("%s", Name.c_str());
	ImGui::Spacing();
	ImGui::TextDisabled("Asset Path");
	ImGui::TextWrapped("%s", Editor.GetAssetPath().c_str());
	ImGui::Separator();

	// 현재 화면에서 선택된 LOD가 아닌 원본 LOD0의 통계를 표시합니다.
	const FStaticMeshData& Data = Mesh->GetMeshData();
	ImGui::Text("Vertices: %d", static_cast<int>(Data.Vertices.Num()));
	ImGui::Text("Triangles: %d", static_cast<int>(Data.Indices.Num() / 3));
	ImGui::Text("Materials: %d", static_cast<int>(Mesh->Materials.Num()));
	ImGui::Text("Sections: %d", static_cast<int>(Data.Sections.Num()));
}
