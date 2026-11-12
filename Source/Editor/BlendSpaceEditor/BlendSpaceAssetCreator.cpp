#include "EnginePCH.h"
#include "BlendSpaceAssetCreator.h"

#include <memory>
#include <commdlg.h>
#include "Asset/AssetUtils.h"

#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"

#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"
#include "imgui.h"

void FBlendSpaceAssetCreator::DrawContextMenu(const FString& AssetPath, const std::filesystem::path& Directory)
{
    URenderAsset* Loaded = UAssetManager::FindLoadedAsset(AssetPath);
    const bool bSkeletalMesh = (Loaded && Loaded->IsA<USkeletalMesh>()) ||
        UAssetManager::GetUnloadedAssetType(AssetPath) == EAssetType::SkeletalMesh;

    if (!bSkeletalMesh)
    {
        return;
    }

    if (ImGui::BeginPopupContextItem("##BlendSpaceContext"))
    {
        DrawMenuItems(AssetPath, Directory);
        ImGui::EndPopup();
    }
}


void FBlendSpaceAssetCreator::DrawMenuItems(const FString& AssetPath, const std::filesystem::path& Directory)
{
    URenderAsset* Loaded = UAssetManager::FindLoadedAsset(AssetPath);
    const bool bSkeletalMesh = (Loaded && Loaded->IsA<USkeletalMesh>()) || UAssetManager::GetUnloadedAssetType(AssetPath) == EAssetType::SkeletalMesh;

    if (bSkeletalMesh && ImGui::MenuItem("Create BlendSpace 1D"))
    {
        PreviewMeshPath = AssetPath;
        CreateDirectory = Directory;
        bOpenDialog = true;
    }
}

void FBlendSpaceAssetCreator::ProcessCreateRequest()
{
    if (!bOpenDialog)
    {
        return;
    }
    bOpenDialog = false;

    wchar_t FilePath[MAX_PATH] = L"NewBlendSpace.blendspace";
    const std::wstring InitialDirectory = std::filesystem::absolute(CreateDirectory).wstring();

    OPENFILENAMEW Dialog = {};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = GetActiveWindow();
    Dialog.lpstrFile = FilePath;
    Dialog.nMaxFile = static_cast<DWORD>(std::size(FilePath));
    Dialog.lpstrFilter = L"BlendSpace Files (*.blendspace)\0*.blendspace\0";
    Dialog.lpstrDefExt = L"blendspace";
    Dialog.lpstrInitialDir = InitialDirectory.c_str();
    Dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&Dialog))
    {
        return;
    }

    const std::filesystem::path Target(FilePath);
    if (!BlendSpaceFile::IsBlendSpacePath(Target.generic_string()))
    {
        MessageBoxW(Dialog.hwndOwner, L"Use the .blendspace extension.", L"Create BlendSpace", MB_OK | MB_ICONERROR);
        return;
    }
    CreateAsset(Target);
}

bool FBlendSpaceAssetCreator::CreateAsset(const std::filesystem::path& Target)
{
    auto BlendSpace = std::unique_ptr<UBlendSpace1D>(NewObject<UBlendSpace1D>());
    BlendSpace->SetPreviewMeshPath(PreviewMeshPath);
    FString ErrorMessage;
    if (!BlendSpaceFile::Write(Target, *BlendSpace, ErrorMessage))
    {
        HTR_LOG(Warning, "Failed to create BlendSpace: {}", ErrorMessage);
        MessageBoxW(GetActiveWindow(), L"Failed to save BlendSpace. See Output Log.", L"Create BlendSpace", MB_OK | MB_ICONERROR);
        return false;
    }
    if (OnCreated)
        OnCreated(FAssetUtils::GetAssetKey(Target.generic_string()));
    return true;
}
