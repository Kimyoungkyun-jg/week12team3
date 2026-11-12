#pragma once

#include "Containers/Array.h"
#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/EditorUI/EditorTabContainer.h"
#include "Editor/LevelEditor/LevelEditorTab.h"

class FEditorManager
{
public:
	explicit FEditorManager(class UEditorEngine& InEditorEngine);

	// 애셋을 살펴볼 수 있는 새로운 에디터를 요청합니다. 
	void OpenEditor(const FString& AssetPath);
	
	// 에디터 전체의 Tick을 수행합니다.
	void Tick(float DeltaTime);

	// 애셋 에디터의 프리뷰 장면을 렌더링합니다.
	void RenderPreview();

	// 지연된 요청을 처리합니다. UI 렌더링이 종료된 이후에 실행합니다.
	void LateTick();

	// 매니저를 정리합니다.
	void Shutdown();

	// 탭의 소유권은 호출자가 유지한다.
	void AddEditorTab(IEditorTab* Tab) { TabContainer.AddTab(Tab); }
	void RemoveEditorTab(IEditorTab* Tab) { TabContainer.RemoveTab(Tab); }
	void ActivateEditorTab(IEditorTab* Tab) { TabContainer.ActivateTab(Tab); }
	void RequestCloseEditorTab(IEditorTab* Tab) { TabContainer.RequestCloseTab(Tab); }

	IEditorTab* GetMainSelectedTab() const { return TabContainer.GetMainSelectedTab(); }
	IEditorTab* GetFocusedTab() const { return TabContainer.GetFocusedTab(); }

	// 에디터 탭과 패널 UI를 그립니다.
	void DrawEditorUI();

	FLevelEditorTab& GetLevelEditorTab() { return *LevelEditorTab; }

	int32 GetEditorCount() const { return Editors.Num(); }

	void OpenAnimBlueprintPreview(const FString& PreviewMeshPath);

	// 이 경로를 연 에디터. 없으면 nullptr.
	FAssetEditor* FindEditor(const FString& Path) const;

private:
	// 에디터를 생성합니다. 이미 열려 있는 에디터가 있으면 활성화합니다.
	void CreateEditor(const FString& AssetPath);

	UEditorEngine& EditorEngine;
	FEditorTabContainer TabContainer;
	TUniquePtr<FLevelEditorTab> LevelEditorTab;
	
	TArray<TUniquePtr<FAssetEditor>> Editors;
	TArray<FString> PendingOpens;
	bool bShuttingDown = false;

	void CreateAnimBlueprintPreview(const FString& PreviewMeshPath);
	TArray<FString> PendingAnimBlueprintPreviews;
};
