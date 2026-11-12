#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Camera/ViewportCameraMovement.h"

class FStaticMeshEditor : public FAssetEditor
{
public:
	FStaticMeshEditor(
		class UEditorEngine& InEditorEngine,
		class FEditorManager& InEditorManager,
		const FString& InAssetPath,
		class UStaticMesh& InMesh
	);
	
	////////////////////////////////////////////////////////////
	// 에디터 공통 인터페이스
	////////////////////////////////////////////////////////////

	// 틱을 돌립니다.
	void Tick(float DeltaTime) override;
	
	// 현재 상태를 렌더링합니다.
	void OnRenderPreview() override;
	

	////////////////////////////////////////////////////////////
	// 에디터 전용 함수
	////////////////////////////////////////////////////////////

	// 카메라를 메시에 포커싱합니다.
	void FocusMesh();

	// 뷰포트 상태를 설정합니다. (뷰포트가 보이지 않으면 입력을 무시합니다)
	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input);


	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const char* GetEditorTypeName() const override { return "StaticMesh"; }
	UStaticMesh* GetMesh() const { return Mesh; }

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;

	// 리소스를 초기화합니다.
	bool InitializeResources() override;

	// 리소스를 정리합니다.
	void ShutdownResources() override;

private:
	class UStaticMesh* Mesh = nullptr;
	class UStaticMeshComponent* MeshComponent = nullptr;
	
	FViewportCameraInput CameraInput;
	
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	
	bool bViewportVisible = false;
	bool bInitialFocusPending = true;
};
