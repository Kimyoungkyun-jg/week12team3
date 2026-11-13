#pragma once

#include "Engine/World.h"

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);

	// Lua 스크립트 열기 대화상자. 취소하면 빈 문자열 (TEAM5)
	static FString OpenScriptDialog(const FString& InitialFolder);

	static FString CurrentScenePath;
private:
	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

};

