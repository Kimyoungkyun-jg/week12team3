#include "EnginePCH.h"
#include "Editor/EditorUI/EditorSort.h"
#include "Editor/EditorUI/EditorUI.h"
#include <imgui_internal.h>   // DockBuilder

namespace
{
	// 저장된 배치가 없거나 사용자가 초기화할 때 사용하는 기본 배치.
	// 가운데 Viewports, 오른쪽 Outliner/Details, 아래 콘텐츠·로그·시퀀서, 왼쪽 설정.
	void BuildDefaultDockLayout(ImGuiID DockspaceId, const ImVec2& Size)
	{
		ImGui::DockBuilderRemoveNode(DockspaceId);
		ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockspaceId, Size);

		ImGuiID Center = DockspaceId;
		ImGuiID Right = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Right, 0.22f, nullptr, &Center);
		const ImGuiID RightBottom = ImGui::DockBuilderSplitNode(Right, ImGuiDir_Down, 0.55f, nullptr, &Right);
		const ImGuiID Bottom = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Down, 0.28f, nullptr, &Center);
		const ImGuiID Left = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Left, 0.18f, nullptr, &Center);

		ImGui::DockBuilderDockWindow("Viewports", Center);
		ImGui::DockBuilderDockWindow("Outliner", Right);
		ImGui::DockBuilderDockWindow("Details", RightBottom);
		ImGui::DockBuilderDockWindow("Content Drawer", Bottom);
		ImGui::DockBuilderDockWindow("Output Log", Bottom);
		ImGui::DockBuilderDockWindow("Sequencer", Bottom);
		ImGui::DockBuilderDockWindow("Settings", Left);
		ImGui::DockBuilderFinish(DockspaceId);
	}
}


bool FEditorUI::Init(bool bInUseDockSpace, bool bInPassthruCentralNode)
{
	bUseDockSpace = bInUseDockSpace;
	bPassthruCentralNode = bInPassthruCentralNode;

	Toolbar = MakeUnique<FLevelEditorToolbar>();

	return true;
}

void FEditorUI::RegisterPanelSettings()
{
	for (auto& Panel : Panels)
		Panel->RegisterOpenStateSettings(Panel->GetOpenStateSettingsName());

	ImGuiSettingsHandler Handler;
	Handler.TypeName = "LevelEditor";
	Handler.TypeHash = ImHashStr(Handler.TypeName);
	Handler.UserData = this;
	Handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* Settings, const char* Name) -> void*
	{
		return std::strcmp(Name, "Panel") == 0 ? Settings->UserData : nullptr;
	};
	Handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* Entry, const char* Line)
	{
		auto* UI = static_cast<FEditorUI*>(Entry);
		if (std::strcmp(Line, "Open=1") == 0) UI->bLevelEditorOpen = true;
		else if (std::strcmp(Line, "Open=0") == 0) UI->bLevelEditorOpen = false;
	};
	Handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* Settings, ImGuiTextBuffer* Buffer)
	{
		const auto* UI = static_cast<const FEditorUI*>(Settings->UserData);
		Buffer->appendf("[LevelEditor][Panel]\nOpen=%d\n\n", UI->bLevelEditorOpen ? 1 : 0);
	};
	ImGui::AddSettingsHandler(&Handler);
}

void FEditorUI::Tick(float DeltaTime)
{
	for (auto& Panel : Panels)
	{

		Panel->Tick(DeltaTime);
	}
}

void FEditorUI::OnRender()
{
	DrawMainMenuBar();

	const bool bDockingEnabled = bUseDockSpace && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable);
	bool bLevelEditorVisible = !bDockingEnabled;
	if (bDockingEnabled)
	{
		const ImGuiViewport* Viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(Viewport->WorkPos);
		ImGui::SetNextWindowSize(Viewport->WorkSize);
		ImGui::SetNextWindowViewport(Viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
		ImGui::Begin("DockingSpace", nullptr, HostFlags);
		ImGui::PopStyleVar(3);
		ImGui::DockSpace(EditorDocking::RootDockId, ImVec2(0.0f, 0.0f),
			ImGuiDockNodeFlags_NoDockingSplit, &EditorDocking::GetRootWindowClass());
		ImGui::End();

		if (!ImGui::DockBuilderGetNode(EditorDocking::LevelDockId)) bResetLevelLayout = true;
		if (bLevelEditorOpen)
		{
			EditorDocking::PrepareWindow(EditorDocking::LevelWindowName, true);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			bLevelEditorVisible = ImGui::Begin(EditorDocking::LevelWindowName, &bLevelEditorOpen,
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleVar();
			if (!bLevelEditorOpen)
			{
				bLevelEditorVisible = false;
				ImGui::MarkIniSettingsDirty();
			}

			if (bLevelEditorVisible)
			{
				if (Toolbar) Toolbar->Draw();
				if (bResetLevelLayout || !ImGui::DockBuilderGetNode(EditorDocking::LevelDockId))
				{
					BuildDefaultDockLayout(EditorDocking::LevelDockId, ImGui::GetContentRegionAvail());
					bResetLevelLayout = false;
				}
			}
			// Keep the inner dock tree alive when another editor tab is selected.
			const ImGuiDockNodeFlags LevelFlags = !bLevelEditorVisible ? ImGuiDockNodeFlags_KeepAliveOnly
				: bPassthruCentralNode ? ImGuiDockNodeFlags_PassthruCentralNode : ImGuiDockNodeFlags_None;
			ImGui::DockSpace(EditorDocking::LevelDockId, ImVec2(0.0f, 0.0f), LevelFlags, &EditorDocking::GetLevelWindowClass());
			ImGui::End();
		}
		else
		{
			ImGui::DockSpace(EditorDocking::LevelDockId, ImVec2(0.0f, 0.0f),
				ImGuiDockNodeFlags_KeepAliveOnly, &EditorDocking::GetLevelWindowClass());
		}
	}
	else if (Toolbar)
	{
		Toolbar->Draw();
	}

	for (auto& Panel : Panels)
	{
		if (Panel->IsOpen() && (Panel->IsRootPanel() || bLevelEditorVisible))
			Panel->OnRender();
		else
			Panel->OnHidden();
	}
	DrawFbxImportDialog();
}

// 파일 메뉴의 요청을 애플리케이션 콜백으로 전달한다.
void FEditorUI::DrawMainMenuBar()
{
	// 메뉴바 높이만 글자 크기에 맞춰 키운다. 간격을 함께 바꾸면 펼친 메뉴까지 물려받는다 (TEAM5).
	ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetFontSize() * 0.5f);

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene")) { if (OnNewScene) OnNewScene(); }
			if (ImGui::MenuItem("Open Scene...")) { if (OnOpenScene) OnOpenScene(); }

			ImGui::Separator();

			if (ImGui::MenuItem("Save")) { if (OnSaveScene) OnSaveScene(); }
			if (ImGui::MenuItem("Save As...")) { if (OnSaveSceneAs) OnSaveSceneAs(); }

			ImGui::Separator();

			if (ImGui::MenuItem("Import FBX...", nullptr, false, !(IsFbxImportBlocked && IsFbxImportBlocked()))) OpenFbxImportDialog();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			// 패널 등록 순서는 그리기 순서라 그대로 두고, 메뉴에만 이름순으로 보여 준다.
			TArray<IEditorPanel*> SortedPanels;
			for (auto& Panel : Panels)
				SortedPanels.Add(Panel.get());
			EditorSort::SortByName(SortedPanels, [](const IEditorPanel* Panel) { return FString(Panel->GetPanelName()); });
			const auto DrawPanelItems = [&](bool bRootPanels)
			{
				for (IEditorPanel* Panel : SortedPanels)
				{
					if (Panel->IsRootPanel() != bRootPanels) continue;
					const bool bOpen = Panel->IsOpen();
					if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, bOpen))
					{
						Panel->SetOpen(!bOpen);
						if (!bOpen && !bRootPanels)
						{
							bLevelEditorOpen = true;
							ImGui::SetWindowFocus(EditorDocking::LevelWindowName);
						}
					}
				}
			};
			const bool bDockingEnabled = bUseDockSpace && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable);
			if (ImGui::BeginMenu("Editors"))
			{
				if (bDockingEnabled)
				{
					if (ImGui::MenuItem("Level Editor", nullptr, &bLevelEditorOpen))
					{
						ImGui::MarkIniSettingsDirty();
						if (bLevelEditorOpen) ImGui::SetWindowFocus(EditorDocking::LevelWindowName);
					}
					ImGui::Separator();
				}
				DrawPanelItems(true);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Level Panels"))
			{
				DrawPanelItems(false);
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Level Layout", nullptr, false, bDockingEnabled))
			{
				bResetLevelLayout = true;
				bLevelEditorOpen = true;
				ImGui::MarkIniSettingsDirty();
				ImGui::SetWindowFocus(EditorDocking::LevelWindowName);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Build"))
		{
			const bool bCanPackage = !IsPackaging || !IsPackaging();
			if (ImGui::MenuItem("Package Game (Shipping)", nullptr, false, bCanPackage)) { if (OnPackageGame) OnPackageGame(); }
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
	ImGui::PopStyleVar();
}
