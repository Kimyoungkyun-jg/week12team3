#include "EnginePCH.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "StaticMeshEditorTab.h"
#include "Components/StaticMeshComponent.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Launch/LaunchEngineLoop.h"
#include "Engine/World.h"
#include "Math/BoxSphereBounds.h"
#include "Render/Mesh.h"

FStaticMeshEditor::FStaticMeshEditor(
	UEditorEngine& InEditorEngine,
	FEditorManager& InEditorManager,
	const FString& InAssetPath,
	UStaticMesh& InMesh
)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, Mesh(&InMesh)
{
}

bool FStaticMeshEditor::InitializeResources()
{
	// 메시가 없음
	if (!Mesh)
	{
		return false;
	}

	// 메시의 Vertex가 비어있음
	if (Mesh->GetMeshData().Vertices.IsEmpty())
	{
		return false;
	}

	// 메시의 Index가 비어있음
	if (Mesh->GetMeshData().Indices.IsEmpty())
	{
		return false;
	}
	
	// 부모 InitializeResources 호출 (FPreviewScene 초기화 및 세팅)
	if (!FAssetEditor::InitializeResources())
	{
		return false;
	}

	// PreviewScene에 StaticMeshComponent 추가
	auto Component = PreviewScene.AddComponent(UStaticMeshComponent::StaticClass());
	MeshComponent = Cast<UStaticMeshComponent>(Component);
	if (!MeshComponent)
	{
		return false;
	}

	// StaticMeshComponent에 메시를 설정
	MeshComponent->SetStaticMesh(Mesh);

	// 카메라를 메시에 포커싱
	FocusMesh();

	return true;
}

TUniquePtr<IEditorTab> FStaticMeshEditor::CreateTab()
{
	return MakeUnique<FStaticMeshEditorTab>(*this);
}

void FStaticMeshEditor::FocusMesh()
{
	if (!ViewportClient)
	{
		return;
	}

	if (!Mesh)
	{
		return;
	}

	// 메시의 AABB를 가져와서 BoxSphereBounds로 변환
	const FBox& Box = Mesh->GetRenderBounds();
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	FBoxSphereBounds Bound
	{
		.Origin = (Box.Min + Box.Max) * 0.5f,
		.BoxExtent = Extent,
		.SphereRadius = Extent.Size()
	};

	// AABB로 ViewportClient의 카메라를 메시에 포커싱
	ViewportClient->FocusOnBounds(Bound);
}

void FStaticMeshEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
}

void FStaticMeshEditor::Tick(float DeltaTime)
{
	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// 뷰포트가 보이면 카메라 이동을 적용
	if (bViewportVisible)
	{
		FViewportCameraState Camera{
			.Location = ViewportClient->GetViewLocation(),
			.Rotation = ViewportClient->GetViewRotation(),
			.OrthoWidth = ViewportClient->GetOrthoWidth(),
			.ViewWidth = static_cast<float>(ViewportWidth),
			.bPerspective = true
		};
		ApplyViewportCameraMovement(Camera, CameraInput, DeltaTime, 5.0f, 0.2f);
		ViewportClient->SetViewLocation(Camera.Location);
		ViewportClient->SetViewRotation(Camera.Rotation);
	}

	// 카메라 입력 리셋
	CameraInput = {};

	// 부모 Tick 호출 (PreviewScene Tick)
	FAssetEditor::Tick(DeltaTime);
}

void FStaticMeshEditor::OnRenderPreview()
{
	// Viewport가 보이지 않으면 렌더링하지 않음
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender)
	{
		return;
	}

	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// ViewportClient 크기 변경
	ViewportClient->Resize(ViewportWidth, ViewportHeight);
	
	// ViewportClient의 RTV가 없다면 렌더링하지 않음
	if (!ViewportClient->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
	{
		return;
	}

	// FocusMesh가 아직 호출되지 않았다면 호출
	if (bInitialFocusPending)
	{
		FocusMesh();
		bInitialFocusPending = false;
	}

	// 뷰포트 클라이언트에게 그리게 한다.
	ViewportClient->Draw(EditorEngine.GetEngineLoop().GetRenderer());
}

void FStaticMeshEditor::ShutdownResources()
{
	MeshComponent = nullptr;
	Mesh = nullptr;
	FAssetEditor::ShutdownResources();
}
