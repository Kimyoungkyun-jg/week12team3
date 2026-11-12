#include "EnginePCH.h"

#include "BlendSpaceEditor.h"
#include "BlendSpaceEditorTab.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "UObject/Casts.h"

FBlendSpaceEditor::FBlendSpaceEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
    const FString& InAssetPath, UBlendSpace1D* InBlendSpace)
    : FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
    , BlendSpace(InBlendSpace)
{
}

bool FBlendSpaceEditor::InitializeResources()
{
    if (!BlendSpace)
    {
        return false;
    }

    const FString& PreviewPath = BlendSpace->GetPreviewMeshPath();
    if (PreviewPath.empty())
    {
        HTR_LOG(Warning, "BlendSpace has no preview mesh: {}", GetAssetPath());
        return false;
    }

    const FString FilePath = FAssetUtils::ResolveAssetFilePath(PreviewPath);
    FString Error;
    Mesh = Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(FilePath.empty() ? PreviewPath : FilePath, Error));

    if (!Mesh)
    {
        HTR_LOG(Warning, "BlendSpace preview mesh failed: {} ({})", PreviewPath, Error);
        return false;
    }

    if (!InitializePreview())
    {
        return false;
    }

    MeshComponent->SetAnimation(BlendSpace);
    MeshComponent->BlendSpaceInput = 0.0f;
    MeshComponent->RefreshAnimationPose();
    return true;
}
void FBlendSpaceEditor::ShutdownResources()
{
    if (MeshComponent)
    {
        MeshComponent->Stop();
    }
    FSkeletalPreviewEditor::ShutdownResources();
    BlendSpace = nullptr;
}

TUniquePtr<IEditorTab> FBlendSpaceEditor::CreateTab()
{
    return MakeUnique<FBlendSpaceEditorTab>(*this);
}

bool FBlendSpaceEditor::Save()
{
    if (!BlendSpace)
    {
        return false;
    }

    const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
    if (!BlendSpaceFile::Write(FilePath.empty() ? GetAssetPath() : FilePath, *BlendSpace, Message))
    {
        return false;
    }

    Message = "BlendSpace saved.";
    return true;
}