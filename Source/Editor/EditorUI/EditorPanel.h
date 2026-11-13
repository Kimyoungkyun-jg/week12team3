#pragma once

#include "Editor/EditorUI/EditorDocking.h"
#include <cstring>

class IEditorPanel
{
public:
	virtual ~IEditorPanel() = default;
	virtual bool Init() = 0;
	virtual void Tick(float DeltaTime) = 0;
	virtual void OnRender() = 0;

	bool IsOpen() const { return bIsOpen; }
	void SetOpen(bool bOpen)
	{
		if (bIsOpen == bOpen) return;
		bIsOpen = bOpen;
		if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->SettingsLoaded)
			ImGui::MarkIniSettingsDirty();
	}
	virtual const char* GetPanelName() const = 0;
	virtual const char* GetOpenStateSettingsName() const { return GetPanelName(); }
	virtual bool IsRootPanel() const { return false; }
	virtual void OnHidden() {}

protected:
	bool BeginPanel(ImGuiWindowFlags Flags = 0)
	{
		return BeginPanel(GetPanelName(), Flags);
	}

	bool BeginPanel(const char* Title, ImGuiWindowFlags Flags = 0)
	{
		PreparePanelWindow(Title);
		bool bOpen = IsOpen();
		const bool bVisible = ImGui::Begin(Title, &bOpen, Flags);
		SetOpen(bOpen);
		return bVisible && bOpen;
	}

	void PreparePanelWindow(const char* Title) const
	{
		EditorDocking::PrepareWindow(Title, IsRootPanel());
	}

	// 첫 NewFrame 전에 등록한다. TypeName은 ini 섹션 이름이며 정적 문자열이어야 한다.
	void RegisterOpenStateSettings(const char* TypeName)
	{
		ImGuiSettingsHandler Handler;
		Handler.TypeName = TypeName;
		Handler.TypeHash = ImHashStr(TypeName);
		Handler.UserData = this;
		Handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* Settings, const char* Name) -> void*
		{
			return std::strcmp(Name, "Panel") == 0 ? Settings->UserData : nullptr;
		};
		Handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* Entry, const char* Line)
		{
			auto* Panel = static_cast<IEditorPanel*>(Entry);
			if (std::strcmp(Line, "Open=1") == 0) Panel->bIsOpen = true;
			else if (std::strcmp(Line, "Open=0") == 0) Panel->bIsOpen = false;
		};
		Handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* Settings, ImGuiTextBuffer* Buffer)
		{
			const auto* Panel = static_cast<const IEditorPanel*>(Settings->UserData);
			Buffer->appendf("[%s][Panel]\nOpen=%d\n\n", Settings->TypeName, Panel->IsOpen() ? 1 : 0);
		};
		ImGui::AddSettingsHandler(&Handler);
	}

private:
	friend class FEditorUI;
	bool bIsOpen = true;
};
