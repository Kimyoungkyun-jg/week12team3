#include "EnginePCH.h"
#include "AnimBlueprintAssetCreator.h"
#include "ImGui.h"

#include <commdlg.h>

#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "Render/SkeletalMesh.h"

void FAnimBlueprintAssetCreator::SetCreatedCallback(std::function<void(const FString&)> Callback)
{
    OnCreated = std::move(Callback);
}

void FAnimBlueprintAssetCreator::DrawMenuItems(const FString& AssetPath, const std::filesystem::path& Directory)
{
    URenderAsset* Asset = UAssetManager::FindLoadedAsset(AssetPath);
    const bool bSkeletalMesh = (Asset && Asset->IsA<USkeletalMesh>()) || UAssetManager::GetUnloadedAssetType(AssetPath) == EAssetType::SkeletalMesh;

    if (bSkeletalMesh && ImGui::MenuItem("Create Animation Blueprint"))
    {
        PreviewMeshPath = FAssetUtils::GetAssetKey(AssetPath);
        CreateDirectory = Directory;
        bCreateRequested = true;
    }
}

void FAnimBlueprintAssetCreator::ProcessCreateRequest()
{
    if (!bCreateRequested)
    {
        return;
    }

    bCreateRequested = false;
    wchar_t Buffer[MAX_PATH] = L"NewAnimBlueprint.animblueprint";
    const std::wstring Directory = std::filesystem::absolute(CreateDirectory).wstring();
    OPENFILENAMEW Dialog = {};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = GetActiveWindow();
    Dialog.lpstrFile = Buffer;
    Dialog.nMaxFile = static_cast<DWORD>(std::size(Buffer));
    Dialog.lpstrFilter = L"Animation Blueprint (*.animblueprint)\0*.animblueprint\0";
    Dialog.lpstrDefExt = L"animblueprint";
    Dialog.lpstrInitialDir = Directory.c_str();
    Dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&Dialog))
    {
        return;
    }

    const std::filesystem::path Target(Buffer);
    const FAnimBlueprintData Data = AnimBlueprintFile::CreateDefault(PreviewMeshPath);
    FString Error;

    if (!AnimBlueprintFile::Write(Target, Data, Error))
    {
        HTR_LOG(Warning, "Failed to create AnimBlueprint: {}", Error);
        const std::wstring Message = FPaths::FromUtf8(Error).wstring();
        MessageBoxW(Dialog.hwndOwner, Message.c_str(), L"Create Animation Blueprint", MB_OK | MB_ICONERROR);
        return;
    }

    if (OnCreated)
    {
        OnCreated(FAssetUtils::GetAssetKey(FPaths::ToUtf8(Target)));
    }
}
