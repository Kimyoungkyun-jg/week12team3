#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Engine/World.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Runtime/Render/LineBatcher.h"

FAssetEditor::FAssetEditor(
	UEditorEngine& InEditorEngine,
	FEditorManager& InEditorManager,
	const FString& InAssetPath
)
	: EditorEngine(InEditorEngine)
	, EditorManager(InEditorManager)
	, AssetPath(InAssetPath)
{
}

FAssetEditor::~FAssetEditor()
{
	// 파생 클래스의 자원 정리는 Manager가 소멸 전에 Shutdown으로 수행한다.
	if (Tab)
	{
		EditorManager.RemoveEditorTab(Tab.get());
	}
}

bool FAssetEditor::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	if (bCloseRequested)
	{
		return false;
	}

	if (!InitializeResources())
	{
		Shutdown();
		return false;
	}

	Tab = CreateTab();
	if (!Tab)
	{
		Shutdown();
		return false;
	}

	EditorManager.AddEditorTab(Tab.get());
	bInitialized = true;
	Activate();
	return true;
}

void FAssetEditor::Tick(float DeltaTime)
{
	PreviewScene.Tick(DeltaTime);
}

void FAssetEditor::OnRenderPreview()
{
	// 프리뷰 장면과 뷰포트 렌더링은 후속 단계에서 연결한다.
}

void FAssetEditor::Activate()
{
	if (Tab && !bCloseRequested)
	{
		EditorManager.ActivateEditorTab(Tab.get());
	}
}

void FAssetEditor::SetAssetPath(const FString& InAssetPath)
{
    AssetPath = InAssetPath;
}

void FAssetEditor::Close()
{
	if (bCloseRequested)
	{
		return;
	}

	// 메뉴의 Close 같은 경로도 저장 확인을 거친다 (탭 X 버튼은 탭 컨테이너가 먼저 묻는다)
	if (!ConfirmClose())
	{
		return;
	}

	bCloseRequested = true;
	if (Tab)
	{
		EditorManager.RequestCloseEditorTab(Tab.get());
	}
}

void FAssetEditor::Shutdown()
{
	if (bShutdown)
	{
		return;
	}

	bShutdown = true;
	bCloseRequested = true;
	if (Tab)
	{
		EditorManager.RemoveEditorTab(Tab.get());
		Tab.reset();
	}
	ShutdownResources();
	bInitialized = false;
}

void FAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
}

bool FAssetEditor::InitializeResources()
{
	if (!PreviewScene.Initialize(&EditorEngine))
	{
		return false;
	}

	// 새로운 ViewportClient 생성
	ViewportClient = MakeUnique<FAssetEditorViewportClient>(*this, PreviewScene);
	ViewportClient->SetViewRotation(FRotator(30.0f, 210.0f, 0.0f));

	// Scene 세팅
	PreviewScene.Setup();

	return true;
}

TUniquePtr<IEditorTab> FAssetEditor::CreateTab()
{
	return MakeUnique<FAssetEditorTab>(*this);
}

void FAssetEditor::ShutdownResources()
{
	ViewportClient.reset();
	PreviewScene.Release();
}

FAssetEditorViewportClient::FAssetEditorViewportClient(FAssetEditor& InAssetEditor, FPreviewScene& InPreviewScene)
	: FEditorViewportClient(InPreviewScene)
	, AssetEditor(InAssetEditor)
{
	// 미리보기 화면의 표시 설정. 한 번 정하면 바뀌지 않으므로 생성할 때 채운다.
	EngineShowFlags.bEnableFog = false;
	EngineShowFlags.bEnableFXAA = true;
	EngineShowFlags.bShowVignette = false;
	EngineShowFlags.bShowCameraOverlay = false;
	EngineShowFlags.RenderingPath = ERenderingPath::Forward;   // 웨이트 히트맵은 Forward 패스에만 있다
}

// 미리보기 씬 위에 에디터가 주는 선과 기즈모를 올린다. 씬은 기반 클래스의 Draw가 그린다.
void FAssetEditorViewportClient::Draw(const FSceneView* View, FScenePassCallbacks& Callbacks)
{
	// 선은 미리보기 월드의 LineBatcher로 그리고, 기즈모는 에디터가 가진 기즈모가 스스로 그린다.
	FLineBatcher* LineBatcher = GetWorld()->LineBatcher.get();

	// TODO: 지금은 EditorGizmo 슬롯에 들어있지만
	// 추후 AssetEditor 전용 Gizmo 슬롯을 만들어서 그쪽으로 옮기는 게 좋을 듯
	Callbacks.Overlays.EditorGizmo = [this, View, LineBatcher]
	{
		LineBatcher->BeginFrame();
		AssetEditor.OnRenderLine(*LineBatcher);
		LineBatcher->OnRender(View->ViewProjectionMatrix);

		if (const FGizmo* PreviewGizmo = AssetEditor.GetPreviewGizmo())
		{
			PreviewGizmo->Render(View);
		}
	};
}
