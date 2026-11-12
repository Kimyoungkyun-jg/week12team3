#include "EnginePCH.h"
#include "AnimBrowserPanels.h"
#include "Editor/SkeletalMeshEditor/Panels/BoneHierarchyPanel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"

#include <algorithm>
#include <filesystem>

namespace
{
    // 미리보기 메시 경로는 표기가 달라도 같은 파일이면 같게 본다.
    FString ResolveMeshPath(const FString& Path)
    {
        const FString Resolved = FAssetUtils::ResolveAssetFilePath(Path);
        return Resolved.empty() ? Path : Resolved;
    }
}

void FAnimSkeletonTreePanel::DrawContent()
{
    FBoneHierarchyPanel::DrawTree(Editor);
}

void FAnimAssetBrowserPanel::Refresh()
{
    namespace fs = std::filesystem;

    bScanned = true;
    Entries.Empty();

    const UAnimSequence* Current = Editor.GetSequence();
    if (!Current || Current->GetPreviewMeshPath().empty())
    {
        return;
    }
    const FString CurrentMesh = ResolveMeshPath(Current->GetPreviewMeshPath());

    // 같은 미리보기 메시로 임포트한 애니메이션 = Unreal의 "같은 스켈레톤" 필터
    std::error_code Ec;
    for (fs::recursive_directory_iterator It("Assets", fs::directory_options::skip_permission_denied, Ec), End;
        !Ec && It != End; It.increment(Ec))
    {
        if (!It->is_regular_file(Ec))
        {
            continue;
        }
        const FString Path = FPaths::ToUtf8(It->path());
        if (!AnimSequenceFile::IsAnimSequencePath(Path))
        {
            continue;
        }
        const UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Path);
        if (Sequence && ResolveMeshPath(Sequence->GetPreviewMeshPath()) == CurrentMesh)
        {
            Entries.Add(Path);
        }
    }

    std::sort(Entries.begin(), Entries.end());
}

void FAnimAssetBrowserPanel::DrawContent()
{
    if (!bScanned)
    {
        Refresh();
    }

    if (ImGui::Button("Refresh"))
    {
        Refresh();
    }
    ImGui::SameLine();
    Filter.Draw("##AnimFilter", -FLT_MIN);

    if (Editor.IsDirty())
    {
        ImGui::TextDisabled("Save or revert to open another animation.");
    }

    if (ImGui::BeginChild("AnimAssetList"))
    {
        for (const FString& Path : Entries)
        {
            const FString Name = std::filesystem::path(Path).filename().string();
            if (!Filter.PassFilter(Name.c_str()))
            {
                continue;
            }

            const bool bCurrent = Path == Editor.GetAssetPath();
            ImGui::PushID(Path.c_str());
            if (ImGui::Selectable(Name.c_str(), bCurrent, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                Editor.OpenFromAssetBrowser(Path);
            }
            ImGui::SetItemTooltip("%s", Path.c_str());
            ImGui::PopID();
        }
        if (Entries.IsEmpty())
        {
            ImGui::TextDisabled("No animations for this preview mesh.");
        }
    }
    ImGui::EndChild();
}
