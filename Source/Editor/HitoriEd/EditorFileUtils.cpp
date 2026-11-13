#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include <commdlg.h>
#include "Serialization/JsonArchive.h"
 
FString FEditorFileUtils::CurrentScenePath = "";

namespace
{
	// 파일 대화상자 버퍼 크기(바이트). UTF-8 한글은 글자당 3바이트이고 긴 경로도 받아야 하므로 MAX_PATH보다 크게 잡는다.
	constexpr DWORD DialogPathCapacity = 32768;
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
	// 긴 경로를 받을 수 있도록 MAX_PATH 대신 넉넉한 버퍼를 쓴다. 매니페스트 덕분에 *A API는 UTF-8이다.
	char FilePath[DialogPathCapacity] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = DialogPathCapacity;

	ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0";
	ofn.lpstrDefExt = "scene";
	// 상대 경로는 대화상자가 무시할 수 있어 절대 경로로 넘긴다.
	const std::string InitialDir = std::filesystem::absolute(ContentPaths::SceneFolder).make_preferred().string();
	ofn.lpstrInitialDir = InitialDir.c_str();

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

FString FEditorFileUtils::OpenScriptDialog(const FString& InitialFolder)
{
	// 긴 경로를 받을 수 있도록 MAX_PATH 대신 넉넉한 버퍼를 쓴다. 매니페스트 덕분에 *A API는 UTF-8이다.
	char FilePath[DialogPathCapacity] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = DialogPathCapacity;

	ofn.lpstrFilter = "Lua Scripts (*.lua)\0*.lua\0";
	ofn.lpstrDefExt = "lua";
	ofn.lpstrInitialDir = InitialFolder.c_str();

	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn))
		return FString(FilePath);

	return FString();
}

FString FEditorFileUtils::OpenLoadSceneDialog()
{
	// 긴 경로를 받을 수 있도록 MAX_PATH 대신 넉넉한 버퍼를 쓴다. 매니페스트 덕분에 *A API는 UTF-8이다.
	char FilePath[DialogPathCapacity] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;

	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = DialogPathCapacity;

	ofn.lpstrFilter =
		"Scene Files (*.scene)\0*.scene\0"
		"All Files (*.*)\0*.*\0";

	ofn.lpstrDefExt = "scene";
	// 상대 경로는 대화상자가 무시할 수 있어 절대 경로로 넘긴다.
	const std::string InitialDir = std::filesystem::absolute(ContentPaths::SceneFolder).make_preferred().string();
	ofn.lpstrInitialDir = InitialDir.c_str();

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


