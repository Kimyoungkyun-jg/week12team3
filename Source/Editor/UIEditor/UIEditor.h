#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Render/SceneViewTargets.h"
#include "UI/UIDrawList.h"
#include "UI/PanelWidget.h"

// .widget 에셋 하나를 편집합니다.
// 위젯 트리, 선택, 되돌리기, 미리보기 텍스처를 소유하고 패널은 이 클래스의 API만 부릅니다.
class FUIEditor : public FAssetEditor
{
public:
	FUIEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InAssetPath);

	const char* GetEditorTypeName() const override { return "Widget"; }

	// 캔버스 패널이 보인 프레임에만 미리보기 텍스처에 위젯을 그립니다.
	void OnRenderPreview() override;

	static bool IsWidgetPath(const FString& Path);
	// 빈 루트 캔버스만 든 .widget 파일을 만듭니다. FilePath는 "Assets/UI/GameOver.widget" 형식입니다.
	static bool CreateDefaultAsset(const FString& FilePath);

	////////////////////////////////////////////////////////////
	// 편집 명령
	////////////////////////////////////////////////////////////

	void Save();
	void Undo();
	void Redo();
	bool CanUndo() const { return !UndoStack.IsEmpty(); }
	bool CanRedo() const { return !RedoStack.IsEmpty(); }
	void DeleteSelected();

	// 선택된 패널(없으면 선택의 부모, 그것도 없으면 루트) 아래에 새 위젯을 붙입니다.
	UWidget* AddWidget(UClass* WidgetClass);

	// 앵커를 바꿔도 화면 위 위치가 그대로 남도록 오프셋을 다시 계산합니다.
	void SetAnchorsKeepRect(UWidget* Widget, const FVector4& NewAnchors);

	// 트리를 순회하는 도중에는 옮기지 않고 FlushPendingReparent에서 옮깁니다.
	void RequestReparent(UWidget* Widget, UPanelWidget* NewParent) { PendingReparent = { Widget, NewParent }; }

	////////////////////////////////////////////////////////////
	// 프레임 처리 (탭과 패널이 부릅니다)
	////////////////////////////////////////////////////////////

	// 현재 ImGui 창에 포커스가 있을 때만 단축키를 처리합니다. 각 패널이 내용을 그리기 전에 부릅니다.
	void HandleShortcuts();
	void FlushPendingReparent();
	// 편집이 끝난 시점에 되돌리기 스냅샷을 쌓습니다.
	void CommitUndoIfChanged();

	void MarkCanvasVisible() { bCanvasVisible = true; }
	void SetCanvasDragging(bool bDragging) { bCanvasDragging = bDragging; }

	////////////////////////////////////////////////////////////
	// Getter / Setter
	////////////////////////////////////////////////////////////

	UPanelWidget* GetRoot() const { return Root; }
	UWidget* GetSelected() const { return Selected; }
	void SetSelected(UWidget* Widget) { Selected = Widget; }

	const FVector2& GetPreviewSize() const { return PreviewSize; }
	void SetPreviewSize(const FVector2& Size) { PreviewSize = Size; }
	FTexture2D* GetPreviewTexture() const { return PreviewTargets.SceneColor.get(); }

	const FString& GetParentClassName() const { return ParentClassName; }
	void SetParentClassName(const FString& Name);

	const FString& GetFilePath() const { return FilePath; }
	bool IsDirty() const { return bDirty; }

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	// 3D 프리뷰 월드가 필요 없으므로 FAssetEditor의 기본 구현을 부르지 않습니다.
	bool InitializeResources() override;
	void ShutdownResources() override;

private:
	FString Snapshot() const;
	void RestoreSnapshot(const FString& Data);

	FString FilePath;                      // 디스크 입출력과 FWidgetAssetCache 키에 쓰는 프로젝트 상대 경로
	FString ParentClassName = "UUserWidget";
	UPanelWidget* Root = nullptr;
	UWidget* Selected = nullptr;

	FVector2 PreviewSize = FVector2(1920.0f, 1080.0f);
	FSceneViewTargets PreviewTargets;      // SceneColor를 미리보기 텍스처로 씁니다
	FUIDrawList DrawList;
	bool bCanvasVisible = false;
	bool bCanvasDragging = false;

	struct FReparent { UWidget* Widget = nullptr; UPanelWidget* NewParent = nullptr; } PendingReparent;

	TArray<FString> UndoStack;
	TArray<FString> RedoStack;
	FString LastSnapshot;
	bool bDirty = false;
};
