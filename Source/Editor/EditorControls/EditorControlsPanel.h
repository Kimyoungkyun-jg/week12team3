#pragma once
#include "Editor/EditorUI/EditorPanel.h"

#include "Editor/Gizmo/Gizmo.h"

class FViewportsPanel;

// 상단 툴바의 액터 추가·기즈모 조작과, FPS·액터 수를 보여 주는 패널.
class FEditorControlsPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnRender() override;
	void DrawToolbar();
	const char* GetPanelName() const override { return "Editor Controls"; }
	inline void SetGizmo(FGizmo* InGizmo) { Gizmo = InGizmo; }
	inline void SetWorld(UWorld* InWorld) { World = InWorld; }
	inline void SetViewportsPanel(FViewportsPanel* InPanel) { ViewportsPanel = InPanel; }
	inline void SetIsPlayingQuery(std::function<bool()> InQuery) { IsPlayingQuery = InQuery; }
	inline void SetSkipGizmoHotkeyQuery(std::function<bool()> Inquery) { SkipGizmoHotkeyQuery = std::move(Inquery); }
	// 액터를 만든 직후 불린다. 에디터가 새 액터를 선택하는 데 쓴다.
	inline void SetOnActorSpawned(std::function<void(AActor*)> InCallback) { OnActorSpawned = std::move(InCallback); }

	float DeltaTime = 1.0f;
	UWorld* World = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;

	// 편집 중인 View 카메라 앞에 액터를 만든다 (TEAM1). View가 없으면 원점이다.
	AActor* SpawnInFrontOfCamera(UClass* Class);

private:
	static float GetToolbarButtonWidth();
	void DrawAddActorMenu(const ImVec2& ButtonSize);
	void DrawGizmoControls(const ImVec2& ButtonSize);

	FGizmo* Gizmo = nullptr;
	std::function<bool()> IsPlayingQuery;
	std::function<bool()> SkipGizmoHotkeyQuery;
	std::function<void(AActor*)> OnActorSpawned;
};
