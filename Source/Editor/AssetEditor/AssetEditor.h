#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Editor/AssetEditor/FPreviewScene.h"
#include "Editor/Viewports/EditorViewportClient.h"

class FGizmo;

class FAssetEditor
{
public:
	FAssetEditor(
		class UEditorEngine& InEditorEngine,
		class FEditorManager& InEditorManager,
		const FString& InAssetPath
	);
	virtual ~FAssetEditor();
	
	
	////////////////////////////////////////////////////////////
	// 에디터 공통 인터페이스
	////////////////////////////////////////////////////////////

	// Editor를 초기화합니다.
	bool Initialize();

	// 틱을 돌립니다.
	virtual void Tick(float DeltaTime);
	
	// 현재 상태를 렌더링합니다.
	virtual void OnRenderPreview();

	// Editor를 포커싱합니다.
	void Activate();

	// Editor를 종료합니다.
	virtual void Close();

	// Editor를 정리합니다.
	void Shutdown();

	virtual void OnRenderLine(class FLineBatcher& LineBatcher);

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	virtual const char* GetEditorTypeName() const = 0;
	const FString& GetAssetPath() const { return AssetPath; }
	FPreviewScene& GetPreviewScene() { return PreviewScene; }
	FEditorViewportClient* GetViewportClient() const { return ViewportClient.get(); }
	virtual const FGizmo* GetPreviewGizmo() const { return nullptr; }

	bool IsCloseRequested() const { return bCloseRequested; }

	// 저장하지 않은 변경이 있는지. 탭 이름에 *가 붙는다.
	virtual bool IsDirty() const { return false; }

	// 닫아도 되면 true. false면 닫기를 미루고, 파생 에디터가 확인 UI를 띄운 뒤 다시 Close를 부른다.
	virtual bool ConfirmClose() { return true; }

protected:
	void SetAssetPath(const FString& InAssetPath);

	// 이 에디터에 연결된 탭을 생성합니다.
	virtual TUniquePtr<class IEditorTab> CreateTab();

	// 리소스를 초기화합니다.
	virtual bool InitializeResources();
	
	// 리소스를 정리합니다.
	virtual void ShutdownResources();

	UEditorEngine& EditorEngine;
	FEditorManager& EditorManager;
	FPreviewScene PreviewScene;

	TUniquePtr<FEditorViewportClient> ViewportClient;

private:
	FString AssetPath;
	TUniquePtr<class IEditorTab> Tab;
	bool bInitialized = false;
	bool bShutdown = false;
	bool bCloseRequested = false;
};

// 에셋 에디터 미리보기 뷰포트의 클라이언트. 미리보기 씬을 그리고 그 위에 에디터가 주는 선과 기즈모를 올린다.
// UE는 에셋 에디터마다 FEditorViewportClient를 상속한 클라이언트를 둔다(FStaticMeshEditorViewportClient,
// FAnimationViewportClient). 이 엔진은 에셋 에디터들이 같은 방식으로 그리므로 하나를 같이 쓴다.
// 그래서 이 클래스 이름은 UE에 없는, 이 엔진 고유의 이름이다.
class FAssetEditorViewportClient : public FEditorViewportClient
{
public:
	FAssetEditorViewportClient(FAssetEditor& InAssetEditor, FPreviewScene& InPreviewScene);

	// 씬을 그리는 Draw는 기반 클래스의 것을 그대로 쓴다.
	using FEditorViewportClient::Draw;
	void Draw(const FSceneView* View, FScenePassCallbacks& Callbacks) override;

private:
	// 이 클라이언트를 만든 에디터. 클라이언트보다 오래 산다.
	FAssetEditor& AssetEditor;
};
