#include "EnginePCH.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Editor/BlendSpaceEditor/BlendSpaceEditor.h"
#include "Editor/UIEditor/UIEditor.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpaceFile.h"
#include "Animation/AnimBlueprintFile.h"
#include "Core/Misc/Paths.h"
#include "Editor/AnimBlueprintEditor/AnimBlueprintEditor.h"

#include <filesystem>

FEditorManager::FEditorManager(UEditorEngine& InEditorEngine)
	: EditorEngine(InEditorEngine)
	, LevelEditorTab(MakeUnique<FLevelEditorTab>())
{
	TabContainer.AddTab(LevelEditorTab.get());
}

void FEditorManager::OpenEditor(const FString& AssetPath)
{
	if (bShuttingDown)
	{
		return;
	}

	const FString Path = FAssetUtils::GetAssetKey(AssetPath);
	if (Path.empty())
	{
		HTR_LOG(Warning, "Cannot open an editor for an empty or invalid asset path.");
		return;
	}

	PendingOpens.Add(Path);
}

FAssetEditor* FEditorManager::FindEditor(const FString& Path) const
{
	for (const auto& Editor : Editors)
	{
		if (Editor->GetAssetPath() == Path)
		{
			return Editor.get();
		}
	}

	return nullptr;
}

void FEditorManager::CreateEditor(const FString& AssetPath)
{
	// 이미 열려 있는 에디터가 있으면 활성화
	if (FAssetEditor* Existing = FindEditor(AssetPath))
	{
		Existing->Activate();
		return;
	}

    if (AnimBlueprintFile::IsAnimBlueprintPath(AssetPath))
    {
        const FString FilePath = FAssetUtils::ResolveAssetFilePath(AssetPath);
        FAnimBlueprintData Data;
        FString Error;

        if (!AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? AssetPath : FilePath), Data, Error))
        {
            HTR_LOG(Warning, "Failed to open AnimBlueprint: {} ({})", AssetPath, Error);
            const std::wstring Message = FPaths::FromUtf8(Error).wstring();
            MessageBoxW(GetActiveWindow(), Message.c_str(), L"Open Animation Blueprint", MB_OK | MB_ICONERROR);
            return;
        }

        TUniquePtr<FAssetEditor> Editor = MakeUnique<FAnimBlueprintEditor>(EditorEngine, *this, AssetPath, std::move(Data));

        if (!Editor->Initialize())
        {
            Editor->Shutdown();
            MessageBoxW(GetActiveWindow(), L"Cannot initialize the preview mesh. See Output Log.", L"Open Animation Blueprint", MB_OK | MB_ICONERROR);
            return;
        }

        Editors.Add(std::move(Editor));
        return;
    }

	// 애니메이션 시퀀스는 렌더 에셋이 아니므로 먼저 분기한다
	if (AnimSequenceFile::IsAnimSequencePath(AssetPath))
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(AssetPath);
		if (!Sequence)
		{
			return;     // 경고는 LoadAnimSequence가 남긴다
		}

		TUniquePtr<FAssetEditor> Editor = MakeUnique<FAnimSequenceEditor>(EditorEngine, *this, AssetPath, Sequence);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "Asset editor initialization failed: {}", AssetPath.c_str());
			return;
		}
		Editors.Add(std::move(Editor));
		HTR_LOG(Info, "Opened asset editor: {}", AssetPath.c_str());
		return;
	}

	if (BlendSpaceFile::IsBlendSpacePath(AssetPath))
	{
		UBlendSpace1D* BlendSpace = UAssetManager::LoadBlendSpace(AssetPath);
		if (!BlendSpace)
		{
			return;
		}

		TUniquePtr<FAssetEditor> Editor = MakeUnique<FBlendSpaceEditor>(EditorEngine, *this, AssetPath, BlendSpace);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "BlendSpace editor initialization failed: {}", AssetPath);
			return;
		}
		Editors.Add(std::move(Editor));
		return;
	}

	// 위젯 에셋은 에디터가 파일을 직접 읽는다
	if (FUIEditor::IsWidgetPath(AssetPath))
	{
		TUniquePtr<FAssetEditor> Editor = MakeUnique<FUIEditor>(EditorEngine, *this, AssetPath);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "Widget editor initialization failed: {}", AssetPath);
			return;
		}
		Editors.Add(std::move(Editor));
		HTR_LOG(Info, "Opened asset editor: {}", AssetPath.c_str());
		return;
	}

	// 애셋을 불러온다
	URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(AssetPath);
	
	// AssetPath가 제각각인 경우가 있으므로, 실패해도 여러가지 Key로 재시도한다.
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(AssetPath);
	if (!Asset && !FilePath.empty())
	{
		// 프로젝트 상대 경로로 시도
		const FString ProjectPath = std::filesystem::path(FilePath)
			.lexically_relative(std::filesystem::current_path()).generic_string();
		Asset = UAssetManager::GetAssetByPath<URenderAsset>(ProjectPath);
		
		if (!Asset)
		{
			// 절대 경로로 시도
			Asset = UAssetManager::GetAssetByPath<URenderAsset>(FilePath);
		}
	}
	
	// obj 파일은 AssetManager에 등록되지 않으므로 직접 로드
	if (!Asset && std::filesystem::path(AssetPath).extension() == ".obj")
	{
		Asset = UAssetManager::LoadObjStaticMesh(FilePath);
	}

	// 베이크된 FBX는 파일에 기록된 실제 메시 타입으로 로드합니다.
	if (!Asset && FFbxMeshBake::IsBakedPath(FilePath))
	{
		FString Error;
		Asset = UAssetManager::LoadFbxMesh(FilePath, Error);
		if (!Asset)
		{
			HTR_LOG(Warning, "Failed to load mesh preview: {} ({})", AssetPath, Error);
			return;
		}
	}

	if (!Asset)
	{
		HTR_LOG(Warning, "Asset is not loaded or could not be loaded: {}", AssetPath.c_str());
		return;
	}


	// Editor 불러오기
	TUniquePtr<FAssetEditor> Editor = nullptr;
	if (Asset->IsA<UStaticMesh>())
	{
		UStaticMesh* MeshAsset = Cast<UStaticMesh>(Asset);
		Editor = MakeUnique<FStaticMeshEditor>(EditorEngine, *this, AssetPath, *MeshAsset);
	}

	if (Asset->IsA<USkeletalMesh>())
	{
		USkeletalMesh* MeshAsset = Cast<USkeletalMesh>(Asset);
		Editor = MakeUnique<FSkeletalMeshEditor>(EditorEngine, *this, AssetPath, *MeshAsset);
	}

	// 지원하지 않는 타입
	if (!Editor)
	{
		HTR_LOG(Warning, "Unsupported asset editor type: {}", AssetPath.c_str());
		return;
	}

	// 초기화 실패시 로깅 후 return
	if (!Editor->Initialize())
	{
		Editor->Shutdown();
		HTR_LOG(Warning, "Asset editor initialization failed: {}", AssetPath.c_str());
		return;
	}

	Editors.Add(std::move(Editor));
	HTR_LOG(Info, "Opened asset editor: {}", AssetPath.c_str());
}

void FEditorManager::Tick(float DeltaTime)
{
	if (bShuttingDown)
	{
		return;
	}

	LevelEditorTab->Tick(DeltaTime);

	for (auto& Editor : Editors)
	{
		if (!Editor->IsCloseRequested())
		{
			Editor->Tick(DeltaTime);
		}
	}
}

void FEditorManager::RenderPreview()
{
	for (auto& Editor : Editors)
	{
		if (!Editor->IsCloseRequested())
		{
			Editor->OnRenderPreview();
		}
	}
}

void FEditorManager::LateTick()
{
	// ImGui의 분리 창까지 렌더링한 뒤 호출한다. 이번 프레임 텍스처 참조도 끝난 시점이다.
	for (int32 Index = Editors.Num() - 1; Index >= 0; --Index)
	{
		if (Editors[Index]->IsCloseRequested())
		{
			HTR_LOG(Info, "Closed asset editor: {}", Editors[Index]->GetAssetPath().c_str());
			Editors[Index]->Shutdown();
			Editors.RemoveAt(Index);
		}
	}

	TArray<FString> Requests = std::move(PendingOpens);
	PendingOpens = {};

	for (const FString& Path : Requests)
	{
		CreateEditor(Path);
	}

	TArray<FString> PreviewRequests = std::move(PendingAnimBlueprintPreviews);
	PendingAnimBlueprintPreviews = {};

	for (const FString& Path : PreviewRequests)
	{
		CreateAnimBlueprintPreview(Path);
	}
}

void FEditorManager::Shutdown()
{
	if (bShuttingDown)
	{
		return;
	}

	bShuttingDown = true;
	
	PendingOpens.Empty();
	PendingAnimBlueprintPreviews.Empty();

	for (auto& Editor : Editors)
	{
		Editor->Shutdown();
	}
	Editors.Empty();

	TabContainer.RemoveTab(LevelEditorTab.get());
	LevelEditorTab.reset();
}

//void FEditorManager::DrawEditorUI()
//{
//	TabContainer.OnRender();
//}

void FEditorManager::DrawEditorUI()
{
    TabContainer.OnRender();
}

void FEditorManager::OpenAnimBlueprintPreview(const FString& PreviewMeshPath)
{
	if (bShuttingDown)
	{
		return;
	}

	const FString Path = FAssetUtils::GetAssetKey(PreviewMeshPath);

	if (Path.empty())
	{
		return;
	}

	PendingAnimBlueprintPreviews.Add(Path);
}

void FEditorManager::CreateAnimBlueprintPreview(const FString& PreviewMeshPath)
{
	const FString DocumentKey = PreviewMeshPath + ".animblueprint-preview";

	if (FAssetEditor* Existing = FindEditor(DocumentKey))
	{
		Existing->Activate();
		return;
	}

	TUniquePtr<FAssetEditor> Editor = MakeUnique<FAnimBlueprintEditor>(EditorEngine, *this, PreviewMeshPath);

	if (!Editor->Initialize())
	{
		Editor->Shutdown();
		HTR_LOG(Warning, "AnimBlueprint preview initialization failed: {}", PreviewMeshPath);
		return;
	}

	Editors.Add(std::move(Editor));
}
