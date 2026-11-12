#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/AssetEditor.h"
#include <filesystem>

FAssetEditorTab::FAssetEditorTab(FAssetEditor& InEditor) : Editor(InEditor)
{
	// ### 뒤 ID는 처음 연 경로로 고정한다. 에디터 안에서 애셋이 바뀌어도(애니메이션 뷰어의 Asset Browser) 같은 창으로 남는다.
	TabId = FString("###AssetEditor:") + Editor.GetAssetPath();
}

const char* FAssetEditorTab::GetTabName() const
{
	// 보이는 이름은 지금 경로로 만든다. 경로가 바뀐 때만 다시 만든다.
	if (NamedPath != Editor.GetAssetPath())
	{
		NamedPath = Editor.GetAssetPath();
		const FString Filename = std::filesystem::path(NamedPath).filename().string();
		const FString Suffix = FString(" (") + Editor.GetEditorTypeName() + ")" + TabId;
		TabName = Filename + Suffix;
		DirtyTabName = Filename + "*" + Suffix;
	}
	return Editor.IsDirty() ? DirtyTabName.c_str() : TabName.c_str();
}

bool FAssetEditorTab::ConfirmClose()
{
	return Editor.ConfirmClose();
}

void FAssetEditorTab::OnCloseRequested()
{
	Editor.Close();
}

void FAssetEditorTab::DrawMenuBar()
{
	// 기본 Editor Menu Bar.

	if (ImGui::BeginMenu("Editor"))
	{
		if (ImGui::MenuItem("Close Editor"))
		{
			Editor.Close();
		}

		ImGui::EndMenu();
	}
}

void FAssetEditorTab::OnRender(bool bActive)
{
}
