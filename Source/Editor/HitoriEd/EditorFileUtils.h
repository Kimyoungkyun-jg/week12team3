#pragma once

#include "Engine/World.h"

enum class EFbxImportMode : uint32;

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);
	static void ImportFbx(EFbxImportMode Mode);

	static FString CurrentScenePath;
private:
	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

};

