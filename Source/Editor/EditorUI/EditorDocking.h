#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace EditorDocking
{
	// Keep these IDs stable so existing imgui.ini layouts remain usable.
	inline constexpr ImGuiID RootDockId = 0x524F4F44;  // ROOD
	inline constexpr ImGuiID LevelDockId = 0x4C564C44; // LVLD
	inline constexpr const char* LevelWindowName = "Level Editor###LevelEditorRootWindow";

	inline const ImGuiWindowClass& GetRootWindowClass()
	{
		static const ImGuiWindowClass WindowClass = []
		{
			ImGuiWindowClass Class;
			Class.ClassId = 0x524F4F54; // ROOT
			Class.DockingAllowUnclassed = false;
			Class.DockingAlwaysTabBar = true;
			Class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoDockingSplit;
			return Class;
		}();
		return WindowClass;
	}

	inline const ImGuiWindowClass& GetLevelWindowClass()
	{
		static const ImGuiWindowClass WindowClass = []
		{
			ImGuiWindowClass Class;
			Class.ClassId = 0x4C564C43; // LVLC
			Class.DockingAllowUnclassed = false;
			return Class;
		}();
		return WindowClass;
	}

	// Call immediately before each top-level Begin(), including multi-window panels.
	inline void PrepareWindow(const char* Title, bool bRootWindow)
	{
		const ImGuiWindowClass& WindowClass = bRootWindow ? GetRootWindowClass() : GetLevelWindowClass();
		ImGui::SetNextWindowClass(&WindowClass);
		if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)) return;

		const ImGuiID DefaultDockId = bRootWindow ? RootDockId : LevelDockId;
		// A disabled editor dockspace must not create an implicit floating dock node.
		if (!ImGui::DockBuilderGetNode(DefaultDockId)) return;

		const ImGuiWindow* Window = ImGui::FindWindowByName(Title);
		const ImGuiWindowSettings* Settings = ImGui::FindWindowSettingsByID(ImHashStr(Title));
		const ImGuiID PreviousDockId = Window ? Window->DockId : Settings ? Settings->DockId : 0;
		const ImGuiID PreviousClassId = Window ? Window->WindowClass.ClassId : Settings ? Settings->ClassId : 0;
		bool bIncompatibleDock = PreviousDockId != 0 && PreviousClassId != 0 && PreviousClassId != WindowClass.ClassId;
		if (ImGuiDockNode* Node = ImGui::DockBuilderGetNode(PreviousDockId))
		{
			while (Node->ParentNode) Node = Node->ParentNode;
			bIncompatibleDock |= Node->IsDockSpace() && Node->WindowClass.ClassId != 0
				&& Node->WindowClass.ClassId != WindowClass.ClassId;
		}
		// Class filters govern drag/drop, but do not repair an incompatible saved DockId.
		ImGui::SetNextWindowDockID(DefaultDockId, bIncompatibleDock ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
	}
}
