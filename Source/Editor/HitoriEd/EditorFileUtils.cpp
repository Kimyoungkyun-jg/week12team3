#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include <commdlg.h>
#include "Serialization/JsonArchive.h"
#include "Editor/Asset/FbxImporter/FbxImporter.h"
 
FString FEditorFileUtils::CurrentScenePath = "";

void FEditorFileUtils::ImportFbx(EFbxImportMode Mode)
{
	wchar_t FilePath[32768] = {};
	OPENFILENAMEW Dialog = {};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = GetActiveWindow();
	Dialog.lpstrFile = FilePath;
	Dialog.nMaxFile = static_cast<DWORD>(std::size(FilePath));
	Dialog.lpstrFilter = L"FBX Files (*.fbx)\0*.fbx\0";
	Dialog.lpstrTitle = Mode == EFbxImportMode::Static ? L"Import Static Mesh" : L"Import Skeletal Mesh";
	Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&Dialog))
	{
		const DWORD Error = CommDlgExtendedError();
		if (Error != 0) HTR_LOG(Error, "FBX file dialog failed: {}", Error);
		return;
	}

	// 파일 선택의 UTF-16 경로를 Importer가 받는 UTF-8로 변환한다.
	const auto Utf8 = std::filesystem::path(FilePath).generic_u8string();
	FFbxImportResult Result;
	FString Error;
	if (!FFbxImporter::ImportToAssets(FString(Utf8.begin(), Utf8.end()), Mode, Result, Error))
	{
		HTR_LOG(Error, "FBX Import failed: {}", Error);
		MessageBoxW(Dialog.hwndOwner, L"FBX를 가져오지 못했습니다. Output Log에서 오류를 확인하세요.", L"FBX Import", MB_OK | MB_ICONERROR);
	}
	// 성공·skip·경고는 Importer가 기록하므로 여기서 다시 출력하지 않는다.
}

bool FEditorFileUtils::NewScene(UWorld* World)
{
	if (!World)
		return false;

	World->ClearWorld();

	CurrentScenePath.clear();

	return true;
}

bool FEditorFileUtils::SaveScene(UWorld* World)
{
	if (!World)
		return false;

	if (CurrentScenePath.empty())
	{
		return SaveSceneAs(World);
	}

	if (!FJsonArchive::SaveWorld(World, CurrentScenePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", CurrentScenePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);
		return false;
	}
	HTR_LOG(Info, "Save Scene : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::SaveSceneAs(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenSaveSceneDialog();

	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::SaveWorld(World, FilePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", FilePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);

		return false;
	}

	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Save Scene As : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::LoadScene(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenLoadSceneDialog();

	// 파일 선택 취소
	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::LoadWorld(World, FilePath))
		return false;

	// 이제 이 파일이 현재 Scene
	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Load Scene : {}", CurrentScenePath);

	return true;
}

FString FEditorFileUtils::OpenSaveSceneDialog()
{
	char FilePath[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0";
	ofn.lpstrDefExt = "scene";
	ofn.lpstrInitialDir = "Scene/";

	ofn.Flags =
		OFN_PATHMUSTEXIST |
		OFN_OVERWRITEPROMPT |
		OFN_NOCHANGEDIR;

	if (GetSaveFileNameA(&ofn))
	{
		return FString(FilePath);
	}

	return FString();
}

FString FEditorFileUtils::OpenLoadSceneDialog()
{
	char FilePath[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;

	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = MAX_PATH;

	ofn.lpstrFilter =
		"Scene Files (*.scene)\0*.scene\0"
		"All Files (*.*)\0*.*\0";

	ofn.lpstrDefExt = "scene";
	ofn.lpstrInitialDir = "Scene/";

	ofn.Flags =
		OFN_FILEMUSTEXIST |
		OFN_PATHMUSTEXIST |
		OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn))
	{
		return FString(FilePath);
	}

	return FString();
}


