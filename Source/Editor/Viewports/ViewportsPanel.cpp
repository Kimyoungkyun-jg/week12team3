#include "EnginePCH.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Engine/GameViewportClient.h"

#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include "Render/RenderCommand.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cassert>
#include <format>

#include "Core/Stats/EditorStats.h"
#include "Render/SkinningStats.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/EditorUI/EditorUIScale.h"

namespace {
	constexpr float SplitterThickness = 6.0f;
	constexpr ImU32 SplitterColor = IM_COL32(55, 55, 55, 230);
	constexpr ImU32 SplitterHoverColor = IM_COL32(255, 192, 0, 255);
	constexpr const char* ViewportTypeLabels[] = {
			"Perspective", "Top", "Front", "Right" };

	// Stat Overlay
	constexpr float StatOverlayMargin = 8.0f;
	constexpr float StatOverlayPadding = 6.0f;
	constexpr ImU32 StatOverlayBackgroundColor = IM_COL32(0, 0, 0, 140);
	constexpr ImU32 TitleColor = IM_COL32(255, 210, 60, 255);
	constexpr ImU32 ValueColor = IM_COL32(235, 235, 235, 255);
} // namespace

// 네 View의 렌더 타깃을 최소 크기로 초기화한다.
bool FViewportsPanel::Init() {
	for (int32 i = 0; i < 4; ++i) {
		ViewportClients[i] = MakeUnique<FEditorViewportClient>();
		ViewportClients[i]->SetViewIndex(i);
		ViewportClients[i]->Resize(1, 1);
	}

	// 초기 시점 설정
	ViewportClients[0]->SetViewportType(ELevelViewportType::Perspective);
	ViewportClients[1]->SetViewportType(ELevelViewportType::Top);
	ViewportClients[2]->SetViewportType(ELevelViewportType::Front);
	ViewportClients[3]->SetViewportType(ELevelViewportType::Right);

	return true;
}

void FViewportsPanel::SetRenderingPath(ERenderingPath Path)
{
	RenderingPath = Path;
	if (Path == ERenderingPath::Deferred)
		for (auto& Client : ViewportClients)
			if (Client && Client->GetShadingModel() == EShadingModel::Gouraud)
				Client->SetShadingModel(EShadingModel::Phong);
}

// 패널의 프레임 갱신 인터페이스이며 별도 계산은 하지 않는다.
void FViewportsPanel::Tick(float DeltaTime) { (void)DeltaTime; }

// Core Rect에 맞춰 View 활성 상태와 타깃 크기를 갱신한다.
void FViewportsPanel::SetView(const int32 ViewIndex, const FRect& Rect,
	const bool bActive) {
	assert(ViewIndex >= 0 && ViewIndex < 4);
	FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
	if (!Client)
		return;

	Client->SetRect(Rect);
	const bool bSlotActive = bActive && Rect.Width > 0.0f && Rect.Height > 0.0f;
	Client->SetActive(bSlotActive);
	if (!bSlotActive)
		return;

	const uint32 Width = static_cast<uint32>(std::max(1.0f, Rect.Width));
	const uint32 Height = static_cast<uint32>(std::max(1.0f, Rect.Height));
	if (Width != Client->GetWidth() || Height != Client->GetHeight())
		Client->Resize(Width, Height);
}

// 마우스 위치에서 패널 원점을 빼 로컬 좌표로 바꾼다.
FVector2 FViewportsPanel::GetLocalMousePosition() const {
	POINT Cursor{};
	const ImVec2 Mouse = ::GetCursorPos(&Cursor)
		? ImVec2(static_cast<float>(Cursor.x), static_cast<float>(Cursor.y))
		: ImGui::GetMousePos();
	return { Mouse.x - ContentOrigin.x, Mouse.y - ContentOrigin.y };
}

// 엔진 입력 처리는 ImGui NewFrame보다 먼저 실행되므로 현재 OS 커서로 버튼
// 영역을 검사한다.
bool FViewportsPanel::IsHovered() const {
	if (!IsOpen() || bToolbarPopupOpen || bDepthDragActive) return false;
	const FVector2 LocalMouse = GetLocalMousePosition();
	const ImVec2 ScreenMouse{ LocalMouse.X + ContentOrigin.x, LocalMouse.Y + ContentOrigin.y };
	const auto Contains = [&ScreenMouse](const ImVec2& Min, const ImVec2& Max)
	{
		return ScreenMouse.x >= Min.x && ScreenMouse.x < Max.x && ScreenMouse.y >= Min.y && ScreenMouse.y < Max.y;
	};
	for (int Index = 0; Index < 4; ++Index)
	{
		if (bToolbarVisible[Index] && (Contains(ToolbarMin[Index], ToolbarMax[Index]) || Contains(ToolbarRightMin[Index], ToolbarRightMax[Index])))
			return false;
		if (bDepthLegendVisible[Index] && Contains(DepthLegendMin[Index], DepthLegendMax[Index]))
			return false;
	}
	if (bStatResetButtonVisible && FStatOverlay::IsAnyEnabled()) {
		POINT Cursor{};
		if (::GetCursorPos(&Cursor) && Cursor.x >= StatResetButtonMin.x &&
			Cursor.x < StatResetButtonMax.x && Cursor.y >= StatResetButtonMin.y &&
			Cursor.y < StatResetButtonMax.y)
			return false;
	}
	const FVector2 Mouse = GetLocalMousePosition();
	return bHovered && Mouse.X >= 0.0f && Mouse.Y >= 0.0f &&
		Mouse.X < ContentSize.x && Mouse.Y < ContentSize.y;
}

// 누적 가로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeHorizontalDrag() {
	const float Result = PendingHorizontalDrag;
	PendingHorizontalDrag = 0.0f;
	return Result;
}

// 누적 세로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeVerticalDrag() {
	const float Result = PendingVerticalDrag;
	PendingVerticalDrag = 0.0f;
	return Result;
}

// 대기 Layout 요청을 한 번 반환하고 플래그를 지운다.
bool FViewportsPanel::ConsumeLayoutRequest(ELayoutMode& OutMode,
	int32& OutSingleViewIndex) {
	if (!bHasLayoutRequest)
		return false;
	OutMode = RequestedLayoutMode;
	OutSingleViewIndex = RequestedSingleViewIndex;
	CurrentLayoutMode = RequestedLayoutMode;
	CurrentSingleViewIndex = RequestedSingleViewIndex;
	bHasLayoutRequest = false;
	return true;
}



// View Texture와 Splitter·Layout·Preset UI를 그리고 요청을 기록한다.
void FViewportsPanel::OnRender() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
	if (!BeginPanel(
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse))
	{
		bHovered = false;
		bFocused = false;
		bStatResetButtonVisible = false;
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	ContentOrigin = ImGui::GetCursorScreenPos();
	ContentSize = ImGui::GetContentRegionAvail();
	ContentSize.x = std::max(1.0f, ContentSize.x);
	ContentSize.y = std::max(1.0f, ContentSize.y);
	bHovered = ImGui::IsWindowHovered();
	bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	bStatResetButtonVisible = false;
	std::fill(std::begin(bToolbarVisible), std::end(bToolbarVisible), false);
	std::fill(std::begin(bDepthLegendVisible), std::end(bDepthLegendVisible), false);

	// 전체 캔버스를 한 번 확보한 뒤 각 렌더 타깃을 창 DrawList에 직접 그린다.
	// Image 항목 네 개를 따로 배치하면 ImGui 레이아웃과 클리핑 상태가 삽입 순서에
	// 영향을 받아, Core가 올바른 사각형을 줘도 아래쪽 행이 잘릴 수 있다.
	ImGui::Dummy(ContentSize);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(ContentOrigin,
		{ ContentOrigin.x + ContentSize.x, ContentOrigin.y + ContentSize.y }, true);
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
		if (!Client || !Client->IsActive())
			continue;

		const FRect& Rect = Client->GetRect();
		const ImVec2 ViewMin{ ContentOrigin.x + Rect.X,
			ContentOrigin.y + Rect.Y };
		const ImVec2 ViewMax{ ViewMin.x + Rect.Width,
			ViewMin.y + Rect.Height };

		const bool bGameViewport = GameViewportClient && ViewIndex == GameViewportIndex;
		if (bGameViewport)
		{
			GameViewportClient->SetRect(FRect{
				ViewMin.x,
				ViewMin.y,
				Rect.Width,
				Rect.Height
				});
		}


		if (ViewIndex == 0 && bShowNoCamera && !bGameViewport)
		{
			// 메인 원근 뷰포트 영역을 어두운 회색으로 채움
			DrawList->AddRectFilled(ViewMin, ViewMax, IM_COL32(45, 45, 48, 255));
			const char* WarningText = "No Camera";
			ImVec2 TextSize = ImGui::CalcTextSize(WarningText);
			ImVec2 CenterPos{
				ViewMin.x + (Rect.Width - TextSize.x) * 0.5f,
				ViewMin.y + (Rect.Height - TextSize.y) * 0.5f
			};
			DrawList->AddText(CenterPos, IM_COL32(220, 220, 220, 255), WarningText);
			continue;
		}

		FTexture2D* DisplayTarget = bGameViewport
			? GameViewportClient->GetColorTarget()
			: Client->GetColorTarget();
		if (!DisplayTarget || !DisplayTarget->GetSRV())
			continue;

		DrawList->AddImage(DisplayTarget->GetSRV(), ViewMin, ViewMax);

		// 마우스 클릭 시 활성 뷰포트 설정
		const bool bViewHovered = bHovered && ImGui::IsMouseHoveringRect(ViewMin, ViewMax);
		if (bViewHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
			ActiveViewIndex = ViewIndex;
			ImGui::SetWindowFocus();
			bFocused = true;
		}
	}
	DrawList->PopClipRect();

	if (CurrentLayoutMode == ELayoutMode::QuadSplit && ViewportClients[0] && ViewportClients[1] && ViewportClients[2]) {
		const FRect& Rect0 = ViewportClients[0]->GetRect();
		const FRect& Rect1 = ViewportClients[1]->GetRect();
		const FRect& Rect2 = ViewportClients[2]->GetRect();
		// 분할선 배치 계산
		const float SplitX =
			(Rect0.X + Rect0.Width + Rect1.X) * 0.5f;
		const float SplitY =
			(Rect0.Y + Rect0.Height + Rect2.Y) * 0.5f;

		const ImVec2 VerticalMin{
				ContentOrigin.x + SplitX - EditorUIScale::Px(SplitterThickness) * 0.5f, ContentOrigin.y };
		const ImVec2 VerticalMax{ VerticalMin.x + EditorUIScale::Px(SplitterThickness),
			ContentOrigin.y + ContentSize.y };
		const ImVec2 HorizontalMin{ ContentOrigin.x, ContentOrigin.y + SplitY -
			EditorUIScale::Px(SplitterThickness) * 0.5f };
		const ImVec2 HorizontalMax{ ContentOrigin.x + ContentSize.x,
			HorizontalMin.y + EditorUIScale::Px(SplitterThickness) };

		ImGui::SetCursorScreenPos(VerticalMin);
		ImGui::InvisibleButton("##MultipleViewportsHorizontalSplitter",
			{ EditorUIScale::Px(SplitterThickness), ContentSize.y });
		ImGui::SetCursorScreenPos(HorizontalMin);
		ImGui::InvisibleButton("##MultipleViewportsVerticalSplitter",
			{ ContentSize.x, EditorUIScale::Px(SplitterThickness) });

		const bool bVerticalHovered =
			bHovered && ImGui::IsMouseHoveringRect(VerticalMin, VerticalMax);
		const bool bHorizontalHovered =
			bHovered && ImGui::IsMouseHoveringRect(HorizontalMin, HorizontalMax);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			bDraggingVerticalSplitter = bVerticalHovered;
			bDraggingHorizontalSplitter = bHorizontalHovered;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			bDraggingVerticalSplitter = false;
			bDraggingHorizontalSplitter = false;
		}

		const bool bVerticalHighlighted =
			bDraggingVerticalSplitter || bVerticalHovered;
		const bool bHorizontalHighlighted =
			bDraggingHorizontalSplitter || bHorizontalHovered;
		DrawList->AddRectFilled(VerticalMin, VerticalMax,
			bVerticalHighlighted ? SplitterHoverColor
			: SplitterColor);
		DrawList->AddRectFilled(HorizontalMin, HorizontalMax,
			bHorizontalHighlighted ? SplitterHoverColor
			: SplitterColor);

		if (bDraggingVerticalSplitter)
			PendingHorizontalDrag += ImGui::GetIO().MouseDelta.x;
		if (bDraggingHorizontalSplitter)
			PendingVerticalDrag += ImGui::GetIO().MouseDelta.y;

		if (bVerticalHighlighted && bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		else if (bVerticalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		else if (bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	else {
		bDraggingVerticalSplitter = false;
		bDraggingHorizontalSplitter = false;
	}

	bDepthDragActive = false;
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex) {
		FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
		if (!Client || !Client->IsActive())
			continue;
		const FRect& Rect = Client->GetRect();
		const ImVec2 ViewMin{ ContentOrigin.x + Rect.X, ContentOrigin.y + Rect.Y };
		const ImVec2 ViewMax{ ViewMin.x + Rect.Width, ViewMin.y + Rect.Height };
		const bool bGameViewport = GameViewportClient && ViewIndex == GameViewportIndex;
		ImGui::PushClipRect(ViewMin, ViewMax, true);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(8.0f), EditorUIScale::Px(4.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorUIScale::Px(4.0f), EditorUIScale::Px(4.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorUIScale::Px(4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		// 상단 툴바 버튼과 같은 색으로 보이도록 콤보 본체를 버튼 색으로 칠한다. 화살표는 원래 버튼 색 그대로다.
		// 색은 콤보 본체를 그리는 동안만 바꾼다. 펼친 목록 안의 체크박스·슬라이더는 원래 입력칸 색이어야 한다.
		const auto BeginToolbarCombo = [](const char* Id, const char* Preview, ImGuiComboFlags Flags = 0)
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_Button));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			const bool bOpen = ImGui::BeginCombo(Id, Preview, Flags);
			ImGui::PopStyleColor(3);
			return bOpen;
		};
		ImDrawListSplitter ToolbarLayers;
		ToolbarLayers.Split(DrawList, 2);
		ToolbarLayers.SetCurrentChannel(DrawList, 1);

		const float Inset = EditorUIScale::Px(8.0f);
		const ImVec2 BarOrigin{ ViewMin.x + Inset, ViewMin.y + Inset };
		// 현재 뷰의 오른쪽 경계를 기준으로 줄바꿈한다. 다른 분할 뷰로 넘치지 않는다.
		const float AvailableWidth = std::max(1.0f, Rect.Width - Inset * 2.0f);
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		const auto ComboWidth = [](const char* Label)
		{
			return EditorUIScale::ButtonWidth(Label) + ImGui::GetFrameHeight();
		};

		// 왼쪽 묶음은 "무엇을 보나"(시점·뷰 모드), 오른쪽 묶음은 "어떻게 보나"(카메라·표시 항목·최대화)다.
		const int SelectedType = static_cast<int>(Client->GetViewportType());
		const int Mode = static_cast<int>(Client->GetViewportMode());
		// 라벨 순서는 EViewportMode 값 순서와 같다.
		const char* Labels[] = { "Lit", "Wireframe", "Scene Depth", "World Normal", "Light Culling", "Shadow Cascades", "Shadow Mask", "Unlit" };
		const FString CameraLabel = std::format("Camera {:.1f}", Client->GetCameraSpeed());
		const bool bMaximized = CurrentLayoutMode == ELayoutMode::Single;
		const char* LayoutLabel = bMaximized ? "Restore" : "Maximize";
		const float LeftWidth = ComboWidth("Perspective") + Spacing + ComboWidth(Labels[Mode]);
		const float RightWidth = ComboWidth(CameraLabel.c_str()) + Spacing + ComboWidth("Show") + Spacing + EditorUIScale::ButtonWidth(LayoutLabel);
		// 두 묶음이 한 줄에 들어가면 오른쪽 묶음을 뷰 오른쪽 끝에 붙이고, 아니면 다음 줄 왼쪽에서 시작한다.
		const bool bOneRow = LeftWidth + EditorUIScale::Px(24.0f) + RightWidth <= AvailableWidth;

		struct FBarGroup { ImVec2 Origin; ImVec2 Max; float RowWidth = 0.0f; float RowY = 0.0f; };
		const auto PlaceControl = [&](FBarGroup& Group, float Width)
		{
			Width = std::min(Width, AvailableWidth);
			const float Gap = Group.RowWidth > 0.0f ? Spacing : 0.0f;
			const float UsedFromViewLeft = Group.Origin.x - BarOrigin.x + Group.RowWidth;
			// 한 줄 배치에서 오른쪽 묶음의 마지막 버튼은 정확히 오른쪽 끝에 닿는다.
			// 부동소수점 오차로 아주 조금 넘쳤다고 판정해 줄바꿈하지 않도록 여유를 둔다.
			constexpr float WrapTolerance = 0.5f;
			if (Group.RowWidth > 0.0f && UsedFromViewLeft + Gap + Width > AvailableWidth + WrapTolerance)
			{
				Group.RowWidth = 0.0f;
				Group.RowY += ImGui::GetFrameHeightWithSpacing();
			}
			else
				Group.RowWidth += Gap;
			ImGui::SetCursorScreenPos({ Group.Origin.x + Group.RowWidth, Group.RowY });
			ImGui::SetNextItemWidth(Width);
			Group.RowWidth += Width;
			Group.Max.x = std::max(Group.Max.x, Group.Origin.x + Group.RowWidth);
			Group.Max.y = Group.RowY + ImGui::GetFrameHeight();
		};

		ImGui::PushID(100 + ViewIndex);
		// 드롭다운·메뉴 팝업 공통 여백. 팝업 창은 Begin 시점의 WindowPadding을 쓰므로
		// 하위 메뉴(BeginMenu)를 열 때도 같은 값을 다시 넣어야 한다.
		const ImVec2 MenuWindowPadding{ EditorUIScale::Px(10.0f), EditorUIScale::Px(8.0f) };
		const float MenuItemSpacingY = EditorUIScale::Px(6.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, MenuWindowPadding);
		const auto BeginPaddedMenu = [&MenuWindowPadding](const char* Label)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, MenuWindowPadding);
			const bool bOpen = ImGui::BeginMenu(Label);
			ImGui::PopStyleVar();
			return bOpen;
		};

		FBarGroup Left{ BarOrigin, BarOrigin, 0.0f, BarOrigin.y };
		PlaceControl(Left, ComboWidth("Perspective"));
		if (BeginToolbarCombo("##ViewportType", ViewportTypeLabels[SelectedType])) {
			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, MenuItemSpacingY);
			for (int TypeIndex = 0; TypeIndex < IM_ARRAYSIZE(ViewportTypeLabels); ++TypeIndex) {
				const bool bSelected = TypeIndex == SelectedType;
				if (ImGui::Selectable(ViewportTypeLabels[TypeIndex], bSelected))
					Client->SetViewportType(static_cast<ELevelViewportType>(TypeIndex));
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}

		// 뷰 모드 (UE처럼 Lit/Unlit/Wireframe + 버퍼 시각화). Lit에서만 의미 있는 반사·셰이딩 모델도 여기 둔다.
		const char* ReflectionNames[] = { "Phong", "Blinn-Phong", "Lambert" };
		const char* ReflectionHints[] = { "R dot V", "N dot H", "Diffuse only" };
		const char* ShadingNames[] = { "Gouraud", "Phong" };
		const char* ShadingHints[] = { "Per vertex", "Per pixel" };
		const int Reflection = static_cast<int>(Client->GetReflectionModel());
		const int Shading = static_cast<int>(Client->GetShadingModel());
		const auto ModeItem = [Client, Mode, &Labels](EViewportMode Item)
		{
			const int Index = static_cast<int>(Item);
			if (ImGui::MenuItem(Labels[Index], nullptr, Mode == Index)) Client->SetViewportMode(Item);
		};
		PlaceControl(Left, ComboWidth(Labels[Mode]));
		if (BeginToolbarCombo("##ViewMode", Labels[Mode], ImGuiComboFlags_HeightLargest))
		{
			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, MenuItemSpacingY);
			ModeItem(EViewportMode::Solid);
			if (BeginPaddedMenu("Lighting Model"))
			{
				// Reflection과 Shading에 같은 라벨(Phong)이 있으므로 구획마다 ID 범위를 나눈다.
				ImGui::SeparatorText("Reflection");
				ImGui::PushID("Reflection");
				for (int Index = 0; Index < IM_ARRAYSIZE(ReflectionNames); ++Index)
					if (ImGui::MenuItem(ReflectionNames[Index], ReflectionHints[Index], Index == Reflection))
						Client->SetReflectionModel(static_cast<EReflectionModel>(Index));
				ImGui::PopID();
				ImGui::SeparatorText("Shading");
				ImGui::PushID("Shading");
				for (int Index = 0; Index < IM_ARRAYSIZE(ShadingNames); ++Index)
				{
					const bool bRequiresForward = Index == static_cast<int>(EShadingModel::Gouraud) && RenderingPath == ERenderingPath::Deferred;
					if (ImGui::MenuItem(ShadingNames[Index], ShadingHints[Index], Index == Shading, !bRequiresForward))
						Client->SetShadingModel(static_cast<EShadingModel>(Index));
					if (bRequiresForward && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip("Gouraud requires Forward rendering. Set Rendering Path to Forward in Settings.");
				}
				ImGui::PopID();
				ImGui::EndMenu();
			}
			ModeItem(EViewportMode::Unlit);
			ModeItem(EViewportMode::Wireframe);
			ImGui::Separator();
			if (BeginPaddedMenu("Buffer Visualization"))
			{
				ModeItem(EViewportMode::SceneDepth);
				ModeItem(EViewportMode::WorldNormal);
				ImGui::EndMenu();
			}
			if (BeginPaddedMenu("Shadows"))
			{
				ModeItem(EViewportMode::Cascade);
				ModeItem(EViewportMode::ShadowMask);
				ImGui::EndMenu();
			}
			ModeItem(EViewportMode::LightCulling);
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("View mode for this viewport.\nReflection: %s   Shading: %s", ReflectionNames[Reflection], ShadingNames[Shading]);

		const ImVec2 RightOrigin = bOneRow
			? ImVec2{ BarOrigin.x + AvailableWidth - RightWidth, BarOrigin.y }
			: ImVec2{ BarOrigin.x, Left.Max.y + EditorUIScale::Px(12.0f) };
		FBarGroup Right{ RightOrigin, RightOrigin, 0.0f, RightOrigin.y };
		PlaceControl(Right, ComboWidth(CameraLabel.c_str()));
		ImGui::BeginDisabled(bGameViewport);
		ImGui::SetNextWindowSizeConstraints(ImVec2(EditorUIScale::Px(360.0f), 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (BeginToolbarCombo("##ViewportCamera", CameraLabel.c_str(), ImGuiComboFlags_HeightLargest))
		{
			ActiveViewIndex = ViewIndex;
			DrawCameraSettings(*Client);
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Camera speed, sensitivity, and projection for this viewport.");
		// 뷰별 표시 항목 (TEAM5 Show Flags). 모든 표시 토글은 여기 한 곳에 있다.
		PlaceControl(Right, ComboWidth("Show"));
		if (BeginToolbarCombo("##ViewShowFlags", "Show", ImGuiComboFlags_HeightLargest))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorUIScale::Px(8.0f), EditorUIScale::Px(6.0f)));
			FViewShowFlags& Flags = Client->GetShowFlags();
			const char* CurrentGroup = nullptr;
			for (const FViewShowFlagInfo& Info : ViewShowFlagInfos)
			{
				if (!CurrentGroup || std::strcmp(CurrentGroup, Info.Group) != 0)
				{
					if (CurrentGroup) ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(4.0f)));
					CurrentGroup = Info.Group;
					ImGui::SeparatorText(CurrentGroup);
				}
				ImGui::Checkbox(Info.Label, &(Flags.*Info.Member));
				if (Info.Member == &FViewShowFlags::bCascadeBounds && Flags.bCascadeBounds)
					ImGui::TextDisabled("Cascade 0-3: red, yellow, green, blue");
			}
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
		// 쿼드 뷰에서는 이 뷰만 크게, 단일 뷰에서는 쿼드 뷰로 되돌린다.
		PlaceControl(Right, EditorUIScale::ButtonWidth(LayoutLabel));
		if (ImGui::Button(LayoutLabel, ImVec2(std::min(EditorUIScale::ButtonWidth(LayoutLabel), AvailableWidth), 0.0f)))
		{
			RequestedLayoutMode = bMaximized ? ELayoutMode::QuadSplit : ELayoutMode::Single;
			RequestedSingleViewIndex = ViewIndex;
			bHasLayoutRequest = true;
		}
		ImGui::SetItemTooltip(bMaximized ? "Return to the quad view." : "Show only this viewport.");

		ImGui::PopID();
		ImGui::PopStyleVar(); // WindowPadding

		// Scene Depth일 때만 뷰 오른쪽 아래에 깊이 범례를 띄운다. 흰색이 되는 거리는 뷰마다 따로다.
		bDepthLegendVisible[ViewIndex] = false;
		if (!bGameViewport && Client->GetViewportMode() == EViewportMode::SceneDepth)
		{
			const char* LegendLabel = "Scene Depth";
			const float LabelWidth = ImGui::CalcTextSize(LegendLabel).x;
			const float ZeroWidth = ImGui::CalcTextSize("0").x;
			const float GradientWidth = EditorUIScale::Px(96.0f);
			const float DragWidth = EditorUIScale::Px(72.0f);
			const float Gap = EditorUIScale::Px(6.0f);
			const float LegendWidth = LabelWidth + Gap + ZeroWidth + Gap + GradientWidth + Gap + DragWidth;
			const float FrameHeight = ImGui::GetFrameHeight();
			const ImVec2 LegendMin{ std::max(BarOrigin.x, ViewMax.x - Inset - LegendWidth), ViewMax.y - Inset - FrameHeight };
			const float TextY = LegendMin.y + (FrameHeight - ImGui::GetTextLineHeight()) * 0.5f;
			float X = LegendMin.x;
			DrawList->AddText({ X, TextY }, ImGui::GetColorU32(ImGuiCol_Text), LegendLabel);
			X += LabelWidth + Gap;
			DrawList->AddText({ X, TextY }, ImGui::GetColorU32(ImGuiCol_TextDisabled), "0");
			X += ZeroWidth + Gap;
			const float BarHalf = EditorUIScale::Px(4.0f);
			const float BarMidY = LegendMin.y + FrameHeight * 0.5f;
			DrawList->AddRectFilledMultiColor({ X, BarMidY - BarHalf }, { X + GradientWidth, BarMidY + BarHalf },
				IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));
			DrawList->AddRect({ X, BarMidY - BarHalf }, { X + GradientWidth, BarMidY + BarHalf }, ImGui::GetColorU32(ImGuiCol_Border));
			X += GradientWidth + Gap;
			ImGui::SetCursorScreenPos({ X, LegendMin.y });
			ImGui::SetNextItemWidth(DragWidth);
			float DepthRange = Client->GetMaxRange();
			ImGui::PushID(200 + ViewIndex);
			// 툴바 콤보처럼 값 칸 바탕만 버튼 색으로 칠해 다른 뷰포트 컨트롤과 맞춘다.
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_Button));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (ImGui::DragFloat("##SceneDepthRange", &DepthRange, 0.5f, 0.1f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
				Client->SetMaxRange(DepthRange);
			ImGui::PopStyleColor(3);
			bDepthDragActive |= ImGui::IsItemActive();
			ImGui::SetItemTooltip("Distance shown as white in this viewport. Drag, or double-click to type.");
			ImGui::PopID();
			const float LegendPadding = EditorUIScale::Px(6.0f);
			DepthLegendMin[ViewIndex] = { LegendMin.x - LegendPadding, LegendMin.y - LegendPadding };
			DepthLegendMax[ViewIndex] = { X + DragWidth + LegendPadding, LegendMin.y + FrameHeight + LegendPadding };
			bDepthLegendVisible[ViewIndex] = true;
		}

		ToolbarLayers.SetCurrentChannel(DrawList, 0);
		const float Padding = EditorUIScale::Px(4.0f);
		const auto DrawBarBackground = [&](const ImVec2& Min, const ImVec2& Max)
		{
			// 상단 툴바 그룹(LevelEditorToolbarUI::EndGroup)과 같은 배경색·모서리. 3D 화면 위라 살짝만 비친다.
			DrawList->AddRectFilled(Min, Max, IM_COL32(45, 45, 45, 240), EditorUIScale::Px(4.0f));
		};
		bToolbarVisible[ViewIndex] = true;
		ToolbarMin[ViewIndex] = { Left.Origin.x - Padding, Left.Origin.y - Padding };
		ToolbarMax[ViewIndex] = { Left.Max.x + Padding, Left.Max.y + Padding };
		ToolbarRightMin[ViewIndex] = { Right.Origin.x - Padding, Right.Origin.y - Padding };
		ToolbarRightMax[ViewIndex] = { Right.Max.x + Padding, Right.Max.y + Padding };
		DrawBarBackground(ToolbarMin[ViewIndex], ToolbarMax[ViewIndex]);
		DrawBarBackground(ToolbarRightMin[ViewIndex], ToolbarRightMax[ViewIndex]);
		if (bDepthLegendVisible[ViewIndex])
			DrawBarBackground(DepthLegendMin[ViewIndex], DepthLegendMax[ViewIndex]);
		ToolbarLayers.Merge(DrawList);
		ImGui::PopStyleVar(4);
		ImGui::PopClipRect();
	}

	// 마지막으로 선택된 뷰포트만 오버레이
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex) {
		if (ViewIndex != ActiveViewIndex)
			continue;
		FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
		if (!Client || !Client->IsActive() || Client->IsLightCulling())
			continue;
		// Use the same viewport as the displayed render target during embedded PIE.
		FSceneRenderData& RenderData = GameViewportClient && ViewIndex == GameViewportIndex
			? GameViewportClient->GetRenderData()
			: Client->GetRenderData();
		ImVec2 OverlayMin, OverlayMax;
		if (!DrawStatOverlay(DrawList, { ContentOrigin.x + Client->GetRect().X, ContentOrigin.y + Client->GetRect().Y },
			ViewIndex, RenderData, OverlayMin, OverlayMax))
			continue;
		// 누적 통계가 있을 때만 오버레이 오른쪽 위에 초기화 버튼을 붙인다.
		if (FStatOverlay::IsEnabled(EStatFlags::Profile) ||
			FStatOverlay::IsEnabled(EStatFlags::Decal) ||
			FStatOverlay::IsEnabled(EStatFlags::Skinning) ||
			FStatRegistry::Find(EditorStats::STAT_PickingTime)) {
			ImGui::SetCursorScreenPos({ OverlayMax.x + EditorUIScale::Px(4.0f), OverlayMin.y });
			ImGui::PushID(300 + ViewIndex);
			// 뷰 툴바 버튼과 같은 여백·모서리로 그려 크기와 모양을 맞춘다.
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(8.0f), EditorUIScale::Px(4.0f)));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorUIScale::Px(4.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			if (ImGui::Button("Reset Stats"))
			{
				FStatRegistry::Reset();
				RenderData.DecalStats.ResetHistory();
				FSkinningStats::ResetHistory();
				RenderData.BaseSkeletalDrawGpu = {};
				RenderData.ShadowSkeletalDrawGpu = {};
			}
			ImGui::PopStyleVar(3);
			ImGui::SetItemTooltip("Reset accumulated stats.");
			ImGui::PopID();
			StatResetButtonMin = ImGui::GetItemRectMin();
			StatResetButtonMax = ImGui::GetItemRectMax();
			bStatResetButtonVisible = true;
		}
	}
	DrawCameraSpeedNotice(DrawList);
	bToolbarPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

	ImGui::End();
	ImGui::PopStyleVar();
}

// This popup edits only the client belonging to its viewport toolbar.
void FViewportsPanel::DrawCameraSettings(FEditorViewportClient& Client)
{
	ImGui::Text("Viewport %d - %s", Client.GetViewIndex() + 1,
		ViewportTypeLabels[static_cast<int>(Client.GetViewportType())]);
	const bool bOrthogonal = !Client.IsPerspective();

	ImGui::SeparatorText("Movement");
	float Speed = Client.GetCameraSpeed();
	ImGui::SetNextItemWidth(EditorUIScale::Px(180.0f));
	if (ImGui::SliderFloat("Camera Speed", &Speed, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		Client.SetCameraSpeed(Speed);
	float Sensitivity = Client.GetMouseSensitivity();
	ImGui::SetNextItemWidth(EditorUIScale::Px(180.0f));
	if (ImGui::SliderFloat("Rotate Sensitivity", &Sensitivity, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		Client.SetMouseSensitivity(Sensitivity);

	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(4.0f)));
	ImGui::TextDisabled("Projection");

	// 카메라 투영 설정 표
	if (ImGui::BeginTable("Camera Projection", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Ortho Width").x);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		// FOV 또는 Ortho Width
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text(bOrthogonal ? "Ortho Width" : "FOV");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);

		if (bOrthogonal)
		{
			float OrthoWidth = Client.GetOrthoWidth();
			if (ImGui::DragFloat("##OrthoWidth", &OrthoWidth, 0.1f) && std::isfinite(OrthoWidth))
			{
				OrthoWidth = FMath::Clamp(OrthoWidth, 0.01f, 1000000.0f);
				Client.SetOrthoWidth(OrthoWidth);
			}
		}
		else
		{
			float FOV = Client.GetViewFOV();
			if (ImGui::DragFloat("##FOV", &FOV, 0.1f) && std::isfinite(FOV))
			{
				FOV = FMath::Clamp(FOV, 1.0f, 179.0f);
				Client.SetViewFOV(FOV);
			}
		}

		// 가까운 평면과 먼 평면
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Near");

		ImGui::TableSetColumnIndex(1);
		float Near = Client.GetNearClip();

		ImGui::SetNextItemWidth(-1.0f);
		bool Changed = ImGui::InputFloat("##Near", &Near);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Far");

		ImGui::TableSetColumnIndex(1);
		float Far = Client.GetFarClip();
		ImGui::SetNextItemWidth(-1.0f);
		Changed |= ImGui::InputFloat("##Far", &Far);

		if (Changed && std::isfinite(Near) && std::isfinite(Far) && Near > 0 && Far > Near)
		{
			Client.SetClipPlanes(Near, Far);
		}
		ImGui::EndTable();
	}

	// 카메라 변환 설정 표
	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(4.0f)));
	ImGui::TextDisabled("Transform");

	bool bLocationChanged = false;
	bool bRotationChanged = false;

	if (ImGui::BeginTable("Camera Transform", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Ortho Width").x);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();

		// 카메라 위치
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Location");

		ImGui::TableSetColumnIndex(1);
		float Available = ImGui::GetContentRegionAvail().x;
		float ItemWidth = (Available - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

		FVector Location = Client.GetViewLocation();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationX", &Location.X, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationY", &Location.Y, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationZ", &Location.Z, 0.1f);

		if (bLocationChanged && std::isfinite(Location.X) && std::isfinite(Location.Y) && std::isfinite(Location.Z))
		{
			Client.SetViewLocation(Location);
		}

		ImGui::TableNextRow();

		// 카메라 회전
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Rotation");

		ImGui::TableSetColumnIndex(1);
		FRotator Rotation = Client.GetViewRotation();

		ImGui::SetNextItemWidth(ItemWidth);
		bRotationChanged |= ImGui::DragFloat("##Pitch", &Rotation.Pitch, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bRotationChanged |= ImGui::DragFloat("##Yaw", &Rotation.Yaw, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		ImGui::BeginDisabled(!bOrthogonal);
		bRotationChanged |= ImGui::DragFloat("##Roll", &Rotation.Roll, 0.1f);
		ImGui::EndDisabled();

		if (bRotationChanged && std::isfinite(Rotation.Pitch) && std::isfinite(Rotation.Yaw) && std::isfinite(Rotation.Roll))
		{
			Client.SetViewRotation(Rotation);
		}
		ImGui::EndTable();
	}
}

void FViewportsPanel::ShowCameraSpeed(const int32 ViewIndex, const float Speed)
{
	CameraSpeedViewIndex = ViewIndex;
	CameraSpeedValue = Speed;
	CameraSpeedShownTime = ImGui::GetTime();
}

// View 왼쪽 아래에 반투명 배경과 함께 이동 속도를 1.5초 동안 그린다.
void FViewportsPanel::DrawCameraSpeedNotice(ImDrawList* DrawList) const
{
	constexpr double NoticeSeconds = 1.5;
	const float Margin = EditorUIScale::Px(8.0f);
	const float Padding = EditorUIScale::Px(6.0f);
	if (CameraSpeedViewIndex < 0 || CameraSpeedViewIndex >= 4 || ImGui::GetTime() - CameraSpeedShownTime > NoticeSeconds)
		return;
	const FEditorViewportClient* Client = ViewportClients[CameraSpeedViewIndex].get();
	if (!Client || !Client->IsActive())
		return;

	const FRect& Rect = Client->GetRect();
	const FString Text = std::format("Camera Speed  {:.2f}", CameraSpeedValue);
	const ImVec2 TextSize = ImGui::CalcTextSize(Text.c_str());
	const ImVec2 TextMin{
		ContentOrigin.x + Rect.X + Margin + Padding,
		ContentOrigin.y + Rect.Y + Rect.Height - Margin - Padding - TextSize.y };
	DrawList->AddRectFilled(
		{ TextMin.x - Padding, TextMin.y - Padding },
		{ TextMin.x + TextSize.x + Padding, TextMin.y + TextSize.y + Padding },
		IM_COL32(0, 0, 0, 140), 4.0f);
	DrawList->AddText(TextMin, IM_COL32(235, 235, 235, 255), Text.c_str());
}

// 항목별 줄을 모아 한 번에 배경과 텍스트를 그린다.
bool FViewportsPanel::DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin, int32 ViewIndex,
	const FSceneRenderData& RenderData, ImVec2& OutMin, ImVec2& OutMax) const
{
	if (!DrawList || !FStatOverlay::IsAnyEnabled())
		return false;

	constexpr float BytesPerMegabyte = 1024.0f * 1024.0f;

	// 제목은 UE처럼 노란색, 값은 흰색으로 구분한다.
	struct FStatLine {
		FString Text;
		ImU32 Color;
	};
	TArray<FStatLine> Lines;

	if (FStatOverlay::IsEnabled(EStatFlags::FPS)) {
		Lines.Add({ "FPS", TitleColor });
		Lines.Add(
			{ std::format("  {:.1f} fps", FStatOverlay::GetFPS()), ValueColor });
		Lines.Add({ std::format("  {:.2f} ms", FStatOverlay::GetFrameTimeMs()),
			ValueColor });
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Memory)) {
		Lines.Add({ "Memory", TitleColor });
		Lines.Add({ std::format("  Object  {:.2f} MB ({} allocs)",
			static_cast<double>(
			FStatOverlay::GetObjectAllocationBytes()) /
			BytesPerMegabyte,
			FStatOverlay::GetObjectAllocationCount()),
			ValueColor });
		Lines.Add({ std::format("  Process {:.2f} MB",
			static_cast<double>(
			FStatOverlay::GetProcessWorkingSetBytes()) /
			BytesPerMegabyte),
			ValueColor });
	}

	struct FProfileStatLine {
		FString Text;
		ImU32 Color;
		FString Values[3];
	};
	TArray<FProfileStatLine> ProfileLines;
	if (FStatOverlay::IsEnabled(EStatFlags::Profile)) {
		ProfileLines.Add({ "CPU Profile (ms)", TitleColor,{ "Last", "Avg", "Max" } });

		for (const auto& [Name, Data] : FStatRegistry::GetAll()) {
			if (TStatId{ Name } == EditorStats::STAT_PickingTime)
				continue;

			FProfileStatLine Line{ "  " + FString(Name), ValueColor };
			Line.Values[0] = std::format("{:.2f}", Data.GetLastMs());
			Line.Values[1] = std::format("{:.2f}", Data.GetRecentAverageMs());
			Line.Values[2] = std::format("{:.2f}", Data.GetMaxMs());
			ProfileLines.Add(Line);
		}
	}

	if (const FCycleStatData* PickingData =
		FStatRegistry::Find(EditorStats::STAT_PickingTime)) {
		ProfileLines.Add({ "Picking", TitleColor });
		FProfileStatLine Line{ "  Time (ms)", ValueColor };
		Line.Values[0] = std::format("Last: {:.2f}", PickingData->GetLastMs());
		Line.Values[1] = std::format("Attempts: {}", PickingData->CallCount);
		Line.Values[2] = std::format("Acc.: {:.2f}", PickingData->GetTotalMs());
		ProfileLines.Add(Line);
	}

	const FRenderStats& Stats = RenderData.RenderStats;
	if (FStatOverlay::IsEnabled(EStatFlags::Light)) {
		Lines.Add({ "Light", TitleColor });
		Lines.Add({ std::format("  Total Lights {} ", Stats.DirectionalCount + Stats.PointCount + Stats.SpotCount), ValueColor });
		Lines.Add({ std::format("  Directional {} ", Stats.DirectionalCount), ValueColor });
		Lines.Add({ std::format("  Point {} ", Stats.PointCount), ValueColor });
		Lines.Add({ std::format("  Spot {} ", Stats.SpotCount), ValueColor });

		Lines.Add({ "Shadow Atlas", TitleColor });
		Lines.Add({ std::format("  Atlas Capacity  {:.2f} MB",
			static_cast<double>(Stats.AtlasCapacity) /
			BytesPerMegabyte),
			ValueColor });
		Lines.Add({ std::format("  Shadow Tiles  {:.2f} MB",
			static_cast<double>(Stats.ShadowTiles) /
			BytesPerMegabyte),
			ValueColor });
		Lines.Add({ std::format("  Tile Usage  {:.1f} %",
			static_cast<double>(Stats.ShadowTiles) /
			static_cast<double>(Stats.AtlasCapacity) * 100),
			ValueColor });
		Lines.Add({ std::format("  Shadow Views  Redrawn {} / Cached {}",
			Stats.ShadowViewsRedrawn, Stats.ShadowViewsCached),
			ValueColor });
		Lines.Add({ std::format("  ShadowLightsSkippedOutsideView {}",
			Stats.ShadowLightsSkippedOutsideView),
			ValueColor });
		Lines.Add({ std::format("  ShadowLightsSkippedNoReceiver {}",
			Stats.ShadowLightsSkippedNoReceiver),
			ValueColor });
		Lines.Add({ std::format("  ShadowLightsSkippedNoCaster {}",
			Stats.ShadowLightsSkippedNoCaster),
			ValueColor });
		Lines.Add({ std::format("  ShadowCastersSkippedSmall {}",
			Stats.ShadowCastersSkippedSmall),
			ValueColor });

		if (RenderData.CascadeShadowCount > 0)
		{
			Lines.Add({ "Cascaded Shadow Map", TitleColor });
			Lines.Add({ std::format("  Cascades {}", RenderData.CascadeShadowCount), ValueColor });

			// 첫 번째 Directional의 그림자 뷰들 = 캐스케이드들
			const uint32 DirOffset = RenderData.GetDirectionalLightOffset();
			const bool bHasDirShadow = RenderData.GetDirectionalLightCount() > 0 &&
				DirOffset < static_cast<uint32>(RenderData.VisibleLightShadowViews.Num()) &&
				RenderData.VisibleLightShadowViews[DirOffset] != FSceneRenderData::InvalidShadowView;

			float PrevDistance = 0.0f;
			for (uint32 c = 0; c < RenderData.CascadeShadowCount; ++c)
			{
				const float Far = RenderData.CascadeShadowDistances[c];
				FString Texel = "-";
				if (bHasDirShadow)
				{
					const uint32 ShadowIdx = RenderData.VisibleLightShadowViews[DirOffset] + c;
					if (ShadowIdx < uint32(RenderData.LightShadows.Num()))
						Texel = std::format("{:.3f}", RenderData.LightShadows[ShadowIdx].WorldUnitsPerTexel);
				}
				Lines.Add({ std::format("  C{}  {:.1f} ~ {:.1f}   texel {}", c, PrevDistance, Far, Texel), ValueColor });
				PrevDistance = Far;
			}
			Lines.Add({ std::format("  Cascade Views  Redrawn {} / Cached {}",
				Stats.CascadeViewsRedrawn, Stats.CascadeViewsCached), ValueColor });
		}
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Decal))
	{
		const FDecalStats& DecalStats = RenderData.DecalStats;
		const FDecalFrameStats& Frame = DecalStats.Frame;

		Lines.Add({ "Decal", TitleColor });

		if (!Frame.bValid)
		{
			Lines.Add({
				" Waiting for a successful view render",
				ValueColor
				});
		}
		else if (!Frame.bHasDBufferPasses)
		{
			Lines.Add({
				" DBuffer path inactive",
				ValueColor
				});
		}
		else
		{
			const char* Method =
				Frame.Method ==
				EDecalRenderMethod::DepthReconstruction
				? "Depth Reconstruction"
				: "Mesh Re-rendering";
			Lines.Add({
				std::format(" Mode: {}", Method),
				ValueColor
				});

			Lines.Add({
				std::format(
					" View: {} x {}",
					Frame.Width,
					Frame.Height),
				ValueColor
				});

			Lines.Add({
		   std::format(
			   "  World / Eligible / Submitted: {} / {} / {}",
			   Frame.WorldCount,
			   Frame.EligibleCount,
			   Frame.SubmittedLit + Frame.SubmittedUnlit),
		   ValueColor
				});

			Lines.Add({
				std::format(
					"  Hidden / Rejected: {} / {}",
					Frame.HiddenCount,
					Frame.RenderDataRejected),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Submitted Lit / Unlit: {} / {}",
					Frame.SubmittedLit,
					Frame.SubmittedUnlit),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Receiver Batches / Items: {} / {}",
					Frame.ReceiverBatchCount,
					Frame.ReceiverItemCount),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Draw Calls: {}",
					Frame.DrawCalls),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Submitted Triangles: {}",
					Frame.SubmittedTriangles),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  DBuffer Logical Size: {:.2f} MiB",
					static_cast<double>(Frame.DBufferBytes) /
						(1024.0 * 1024.0)),
				ValueColor
				});

			ProfileLines.Add({
				"Decal CPU (ms)",
				TitleColor,
				{ "Last", "Avg120", "Max120" }
				});

			auto AddCpuLine =
				[&](const char* Name, const FCycleStatData& History)
				{
					FProfileStatLine Line{
						FString("  ") + Name,
						ValueColor
					};

					Line.Values[0] =
						std::format("{:.3f}", History.GetLastMs());

					Line.Values[1] =
						std::format(
							"{:.3f}",
							History.GetRecentAverageMs());

					Line.Values[2] =
						std::format(
							"{:.3f}",
							GetDecalRecentMaxMs(History));

					ProfileLines.Add(Line);
				};

			AddCpuLine("Gather", DecalStats.GatherHistory);
			AddCpuLine("Sort", DecalStats.SortHistory);
			AddCpuLine("Submit", DecalStats.SubmitHistory);
			if (Frame.Method == EDecalRenderMethod::MeshRerendering)
			{
				AddCpuLine("Receiver Selection", DecalStats.ReceiverSelectionHistory);
			}


			ProfileLines.Add({
				"Decal GPU incl. Clear (ms)",
				TitleColor,
				{ "Last", "Avg120", "Max120" }
				});

			auto AddGpuLine =
				[&](const char* Name,
					const std::shared_ptr<FGpuTimingHistory>& History)
				{
					FProfileStatLine Line{
						FString("  ") + Name,
						ValueColor
					};

					if (!History || History->Count == 0)
					{
						Line.Values[0] = "Pending";
						Line.Values[1] = "-";
						Line.Values[2] = "-";
					}
					else
					{
						Line.Values[0] =
							std::format("{:.3f}", History->LastMs);

						Line.Values[1] =
							std::format(
								"{:.3f}",
								History->GetAverageMs());

						Line.Values[2] =
							std::format(
								"{:.3f}",
								History->GetMaxMs());
					}

					ProfileLines.Add(Line);
				};

			AddGpuLine("Lit", DecalStats.GpuLit);
			AddGpuLine("Unlit", DecalStats.GpuUnlit);

			if (DecalStats.GpuLit &&
				DecalStats.GpuLit->Count != 0)
			{
				const uint64 CurrentFrame =
					FGpuProfiler::Get().GetFrameId();

				const uint64 SampleFrame =
					DecalStats.GpuLit->LastFrameId;

				Lines.Add({
					std::format(
						"  GPU Lit sample age: {} frames",
						CurrentFrame - SampleFrame),
					ValueColor
					});
			}

			Lines.Add({
				std::format(
					"  GPU profiler skipped frames/scopes: {} / {}",
					FGpuProfiler::Get().GetSkippedFrames(),
					FGpuProfiler::Get().GetSkippedScopes()),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  GPU profiler errors: {}",
					FGpuProfiler::Get().GetErrorCount()),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  View Frustum Rejected: {}",
					Frame.ViewFrustumRejectedDecals),
				ValueColor
				});

			if (Frame.Method == EDecalRenderMethod::MeshRerendering)
			{
				Lines.Add({
					std::format(
						"  BVH Bounds Tests: {}",
						Frame.MeshBVHBoundsTests),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  BVH Proxy Candidates: {}",
						Frame.MeshBVHProxyCandidates),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Refine Rejected Proxies: {}",
						Frame.MeshRefineRejectedProxies),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Matched Items: {}",
						Frame.MeshMatchedItems),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Fallback Items: {}",
						Frame.MeshFallbackItems),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  No Receiver Decals: {}",
						Frame.MeshNoReceiverDecals),
					ValueColor
					});
			}
			else if (Frame.Method == EDecalRenderMethod::DepthReconstruction)
			{
				Lines.Add({
					std::format(
						"  Volume / Far Fallback: {} / {}",
						Frame.VolumeDrawCalls,
						Frame.FarFallbackDrawCalls),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Frustum Rejected: {}",
						Frame.FrustumRejectedDecals),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Invalid Geometry: {}",
						Frame.InvalidGeometryDecals),
					ValueColor
					});
			}
		}
	}


	if (FStatOverlay::IsEnabled(EStatFlags::Skinning))
	{
		const FSkinningFrameStats& Skin = FSkinningStats::Last();
		const ESkinningMode Effective = USkeletalMeshComponent::GetSkinningMode();

		Lines.Add({ "Skinning", TitleColor });
		Lines.Add({ std::format("  Mode: {}", GetSkinningModeName(Effective)), ValueColor });
		Lines.Add({ std::format("  Components: {} (CPU {} / GPU {})",
			Skin.TotalComponents, Skin.CpuModeComponents, Skin.GpuModeComponents), ValueColor });
		Lines.Add({ std::format("  Mesh Vertices / Bones: {} / {}", Skin.TotalVertices, Skin.TotalBones), ValueColor });
		Lines.Add({ std::format("  Updated This Frame: CPU {} / GPU {}{}",
			Skin.CpuSkinnedComponents, Skin.GpuSkinnedComponents,
			USkeletalMeshComponent::IsForceSkinningUpdate() ? "  (ForceUpdate)" : ""), ValueColor });
		// 포즈가 그대로면 스키닝은 다시 돌지 않아 두 모드의 차이가 보이지 않는다.
		if (Skin.CpuSkinnedComponents + Skin.GpuSkinnedComponents == 0 && Skin.TotalComponents > 0)
			Lines.Add({ "  Pose unchanged: nothing re-skinned. Use r.Skinning.ForceUpdate 1 to compare.", ValueColor });
		Lines.Add({ std::format("  CPU Skinned Vertices: {}", Skin.CpuSkinnedVertices), ValueColor });
		Lines.Add({ std::format("  Bone Matrices Computed: {}", Skin.BoneMatrices), ValueColor });
		Lines.Add({ std::format("  Upload: Vertex {:.1f} KB / Matrix {:.1f} KB",
			static_cast<double>(Skin.VertexUploadBytes) / 1024.0,
			static_cast<double>(Skin.MatrixUploadBytes) / 1024.0), ValueColor });

		ProfileLines.Add({ "Skinning CPU (ms)", TitleColor, { "Last", "Avg120", "Max120" } });
		auto AddSkinCpuLine = [&](const char* Name, const FCycleStatData& History)
			{
				FProfileStatLine Line{ FString("  ") + Name, ValueColor };
				Line.Values[0] = std::format("{:.3f}", History.GetLastMs());
				Line.Values[1] = std::format("{:.3f}", History.GetRecentAverageMs());
				Line.Values[2] = std::format("{:.3f}", GetDecalRecentMaxMs(History));
				ProfileLines.Add(Line);
			};
		AddSkinCpuLine("Bone Matrices", FSkinningStats::BoneMatrixHistory());
		AddSkinCpuLine("Vertex Skinning", FSkinningStats::CpuSkinHistory());
		AddSkinCpuLine("Buffer Upload", FSkinningStats::UploadHistory());
		AddSkinCpuLine("Total", FSkinningStats::TotalHistory());

		ProfileLines.Add({ "Skinning GPU", TitleColor, { "Last", "Avg120", "Max120" } });
		auto AddGpuTimeLine = [&](const char* Name, const FGpuTimingHistory* Timing)
		{
			FProfileStatLine Line{ Name, ValueColor, { "-", "-", "-" } };
			if (Timing && Timing->Count > 0)
			{
				Line.Values[0] = std::format("{:.3f} ms", Timing->LastMs);
				Line.Values[1] = std::format("{:.3f} ms", Timing->GetAverageMs());
				Line.Values[2] = std::format("{:.3f} ms", Timing->GetMaxMs());
			}
			ProfileLines.Add(Line);
		};

		const FGpuTimingHistory* Timing = RenderData.SkeletalDrawGpuMode == Effective
			? RenderData.BaseSkeletalDrawGpu.get() : nullptr;
		AddGpuTimeLine("  Skinning", Timing);
		AddGpuTimeLine("  Shadow Draw", RenderData.SkeletalDrawGpuMode == Effective
			? RenderData.ShadowSkeletalDrawGpu.get() : nullptr);
		AddGpuTimeLine("  CS Dispatch", Effective == ESkinningMode::Compute
			? FGpuProfiler::Get().GetSkinningDispatchTiming() : nullptr);

		FProfileStatLine VSLine{ "  VS Last", ValueColor, { "-", "", "" } };
		if (Effective != ESkinningMode::CPU && Timing && Timing->Count > 0)
			VSLine.Values[0] = std::format("{}", Timing->LastVSInvocations);
		ProfileLines.Add(VSLine);
	}

	if (Lines.Num() == 0 && ProfileLines.Num() == 0)
		return false;

	// Start below this viewport toolbar so the stats never overlap its controls.
	const float LineHeight = ImGui::GetTextLineHeight();
	const ImVec2 Origin{ ViewMin.x + EditorUIScale::Px(StatOverlayMargin),
			std::max(ToolbarMax[ViewIndex].y, ToolbarRightMax[ViewIndex].y) + EditorUIScale::Px(StatOverlayMargin) };

	// 최대 너비 계산
	float MaxWidth = 0.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
		MaxWidth =
		std::max(MaxWidth, ImGui::CalcTextSize(Lines[Index].Text.c_str()).x);

	const float MinValueWidth = ImGui::CalcTextSize("00000.00").x;
	float NameWidth = 0.0f;
	float ValueWidths[3] = { MinValueWidth, MinValueWidth, MinValueWidth };
	bool bHasColumns = false;

	for (const FProfileStatLine& Line : ProfileLines) {
		const float TextWidth = ImGui::CalcTextSize(Line.Text.c_str()).x;
		bHasColumns = true;
		NameWidth = std::max(NameWidth, TextWidth);
		for (int32 Column = 0; Column < 3; ++Column)
			ValueWidths[Column] = std::max(
				MinValueWidth, ImGui::CalcTextSize(Line.Values[Column].c_str()).x);
	}

	float ColumnRight[3]{};
	if (bHasColumns) {
		float Width = NameWidth;
		for (int32 Column = 0; Column < 3; ++Column) {
			Width += ImGui::GetFontSize() + ValueWidths[Column];
			ColumnRight[Column] = Origin.x + Width;
		}
		MaxWidth = std::max(MaxWidth, Width);
	}

	const ImVec2 BackgroundMin{ Origin.x - EditorUIScale::Px(StatOverlayPadding),
			Origin.y - EditorUIScale::Px(StatOverlayPadding) };
	const ImVec2 BackgroundMax{
			Origin.x + MaxWidth + EditorUIScale::Px(StatOverlayPadding),
			Origin.y +
					LineHeight * static_cast<float>(Lines.Num() + ProfileLines.Num()) +
					EditorUIScale::Px(StatOverlayPadding) };
	DrawList->AddRectFilled(BackgroundMin, BackgroundMax,
		StatOverlayBackgroundColor, EditorUIScale::Px(4.0f));

	// Lines 그리기
	for (int32 Index = 0; Index < Lines.Num(); ++Index) {
		DrawList->AddText(
			{ Origin.x, Origin.y + LineHeight * static_cast<float>(Index) },
			Lines[Index].Color, Lines[Index].Text.c_str());
	}

	// ProfileLines 그리기
	for (int32 Index = 0; Index < ProfileLines.Num(); ++Index) {
		const FProfileStatLine& Line = ProfileLines[Index];
		const float Y =
			Origin.y + LineHeight * static_cast<float>(Lines.Num() + Index);
		DrawList->AddText({ Origin.x, Y }, Line.Color, Line.Text.c_str());

		for (int32 Column = 0; Column < 3; ++Column) {
			const FString& Text = Line.Values[Column];
			const float TextWidth = ImGui::CalcTextSize(Text.c_str()).x;
			DrawList->AddText({ ColumnRight[Column] - TextWidth, Y }, Line.Color,
				Text.c_str());
		}
	}
	OutMin = BackgroundMin;
	OutMax = BackgroundMax;
	return true;
}
