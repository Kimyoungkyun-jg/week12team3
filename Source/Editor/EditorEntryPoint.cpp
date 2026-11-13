#include "EnginePCH.h"

#include "Launch/EntryPoint.h"

#include "Editor/HitoriEd/EditorEngine.h"

UClass* GetEngineClass()
{
	return UEditorEngine::StaticClass();
}
