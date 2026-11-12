#include "EnginePCH.h"
#include "StaticMeshSettingsPanel.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"

FStaticMeshSettingsPanel::FStaticMeshSettingsPanel(FStaticMeshEditor& InEditor)
	: FStaticMeshEditorPanel(InEditor, "Settings")
{
}

void FStaticMeshSettingsPanel::DrawContent()
{
	if (FEditorViewportClient* Client = Editor.GetViewportClient())
	{
		bool bWireframe = Client->IsWireframe();
		if (ImGui::Checkbox("Wireframe", &bWireframe))
		{
			Client->SetViewportMode(bWireframe ? EViewportMode::Wireframe : EViewportMode::Solid);
		}
	}
}
