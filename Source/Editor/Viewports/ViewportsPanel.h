#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h" 
#include "Editor/LevelViewportLayout.h"
#include <functional>

class FGameViewportClient;

class FViewportsPanel : public IEditorPanel {
public:
	// 활성 뷰포트 인덱스 설정
	void SetActiveViewIndex(const int32 InIndex) { ActiveViewIndex = InIndex; }
	int32 GetActiveViewIndex() const { return ActiveViewIndex; }
	void SetRenderingPath(ERenderingPath Path);
	ERenderingPath GetRenderingPath() const { return RenderingPath; }

	// 네 View의 렌더 타깃과 UI 제어 상태를 초기화한다.
	bool Init() override;
	// 프레임 입력에서 Splitter Drag와 View별 UI 요청을 수집한다.
	void Tick(float DeltaTime) override;
	// 각 View Texture와 Splitter·Layout 조작 UI를 ImGui 패널에 그린다.
	void OnRender() override;
	const char *GetPanelName() const override { return "Viewports"; }
	void OnHidden() override { bHovered = false; bFocused = false; bStatResetButtonVisible = false; bDepthDragActive = false; }

	// Core가 계산한 View Rect에 맞춰 해당 클라이언트의 활성 상태와 크기를 갱신한다.
	void SetView(int32 ViewIndex, const FRect &Rect, bool bActive);

	FVector2 GetContentSize() const { return {ContentSize.x, ContentSize.y}; }
	// 패널 내용 영역 왼쪽 위의 화면 좌표. View Rect에 더하면 화면 좌표가 된다 (PIE 커서 잠금 범위 계산용)
	FVector2 GetContentScreenOrigin() const { return {ContentOrigin.x, ContentOrigin.y}; }
	FVector2 GetLocalMousePosition() const;
	bool IsHovered() const;
	bool IsFocused() const { return IsOpen() && bFocused; }

	float ConsumeHorizontalDrag();
	float ConsumeVerticalDrag();

	// UI에서 발생한 Layout 변경 요청을 한 번 소비하도록 반환한다.
	bool ConsumeLayoutRequest(ELayoutMode &OutMode, int32 &OutSingleViewIndex);
	void SetLayoutMode(ELayoutMode InMode, int32 InSingleIndex = 0) {
		CurrentLayoutMode = InMode;
		CurrentSingleViewIndex = InSingleIndex;
	}

	// 뷰포트 클라이언트 조회
	FEditorViewportClient* GetViewportClient(const int32 ViewIndex) const
	{
			if (ViewIndex >= 0 && ViewIndex < 4)
			{
					return ViewportClients[ViewIndex].get();
			}
			return nullptr;
	}
	void SetGameViewportClient(int32 ViewIndex, FGameViewportClient* InGameClient) {
		if (ViewIndex < 0 || ViewIndex >= 4)
			return;
		GameViewportIndex = ViewIndex;
		GameViewportClient = InGameClient;
	}
	void ClearGameViewportClient() {
		GameViewportClient = nullptr;
		GameViewportIndex = -1;
	}

	void SetShowNoCamera(bool bValue) { bShowNoCamera = bValue; }

	// 우클릭+휠로 바꾼 카메라 속도를 View 왼쪽 아래에 잠깐 보여 준다 (TEAM1).
	void ShowCameraSpeed(int32 ViewIndex, float Speed);

private:
	int32 ActiveViewIndex = 0;
	ERenderingPath RenderingPath = ERenderingPath::Deferred;
	bool bShowNoCamera = false;

	// 통계 정보 출력
	// 그린 배경 사각형을 OutMin/OutMax로 돌려준다. 보여 줄 통계가 없으면 false다.
	bool DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin, int32 ViewIndex,
		const FSceneRenderData& RenderData, ImVec2& OutMin, ImVec2& OutMax) const;
	void DrawCameraSpeedNotice(ImDrawList* DrawList) const;
	void DrawCameraSettings(FEditorViewportClient& Client);
	int32 CameraSpeedViewIndex = -1;
	float CameraSpeedValue = 0.0f;
	double CameraSpeedShownTime = -100.0;

	TUniquePtr<FEditorViewportClient> ViewportClients[4]{};
	// PIE 월드 컨텍스트가 소유하며, 세션 종료 전에 비운다.
	FGameViewportClient* GameViewportClient = nullptr;
	int32 GameViewportIndex = -1;
	ImVec2 ContentOrigin{};
	ImVec2 ContentSize{1.0f, 1.0f};
	bool bHovered = false;
	bool bFocused = false;
	bool bStatResetButtonVisible = false;
	bool bToolbarPopupOpen = false;
	bool bToolbarVisible[4]{};
	ImVec2 ToolbarMin[4]{};
	ImVec2 ToolbarMax[4]{};
	// 뷰 툴바 오른쪽 묶음(카메라·Show·최대화). 왼쪽 묶음과 따로 입력을 막는다.
	ImVec2 ToolbarRightMin[4]{};
	ImVec2 ToolbarRightMax[4]{};
	// Scene Depth 범례. 해당 모드인 뷰에서만 보인다.
	bool bDepthLegendVisible[4]{};
	ImVec2 DepthLegendMin[4]{};
	ImVec2 DepthLegendMax[4]{};
	// 범례 값을 끄는 동안 커서가 범례 밖으로 나가도 뷰 입력을 막는다.
	bool bDepthDragActive = false;
	ImVec2 StatResetButtonMin{};
	ImVec2 StatResetButtonMax{};
	float PendingHorizontalDrag = 0.0f;
	float PendingVerticalDrag = 0.0f;
	// 누른 뒤 커서가 Splitter 영역을 벗어나도 버튼을 놓을 때까지 드래그를 유지한다.
	bool bDraggingVerticalSplitter = false;
	bool bDraggingHorizontalSplitter = false;
	ELayoutMode CurrentLayoutMode = ELayoutMode::QuadSplit;
	int32 CurrentSingleViewIndex = 0;
	bool bHasLayoutRequest = false;
	ELayoutMode RequestedLayoutMode = ELayoutMode::QuadSplit;
	int32 RequestedSingleViewIndex = 0;

};
