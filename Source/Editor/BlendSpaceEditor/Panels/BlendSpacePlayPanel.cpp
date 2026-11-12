#include "EnginePCH.h"
#include "BlendSpacePlayPanel.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include "Asset/AssetUtils.h"

#include "Editor/BlendSpaceEditor/BlendSpaceEditor.h"
#include "Components/SkeletalMeshComponent.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimSequenceFile.h"

#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"

void FBlendSpacePlayPanel::DrawContent()
{
    if (ImGui::Button("Save"))
        Editor.Save();

    const FString& Message = Editor.GetMessage();
    if (!Message.empty())
        ImGui::TextWrapped("%s", Message.c_str());

    DrawSamples();
    ImGui::SeparatorText("Playback");

    USkeletalMeshComponent* Component = Editor.GetMeshComponent();
    if (!Component)
    {
        ImGui::TextUnformatted("No preview mesh.");
        return;
    }

    UAnimSingleNodeInstance* Instance = Component->GetSingleNodeInstance();
    if (!Instance)
    {
        ImGui::TextUnformatted("No animation instance.");
        return;
    }

    if (ImGui::Checkbox("Loop", &bLoop))
        Instance->SetLooping(bLoop);

    if (ImGui::Button("Play / Resume"))
        Component->Play(bLoop);
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        Component->Stop();

    float Rate = Instance->GetPlayRate();
    if (ImGui::SliderFloat("Play rate", &Rate, -2.0f, 2.0f))
        Instance->SetPlayRate(Rate);
    if (ImGui::DragFloat("Blend input X", &Component->BlendSpaceInput))
        Component->RefreshAnimationPose();

    const float Length = Instance->GetLength();
    float Time = Instance->GetCurrentTime();
    if (Length > 0.0f && ImGui::SliderFloat("Time", &Time, 0.0f, Length))
    {
        Component->Stop();
        Instance->SetPosition(Time);
        Component->RefreshAnimationPose();
    }

    ImGui::Text("%s | %.3f / %.3f seconds",
        Component->IsPlaying() ? "Playing" : "Stopped",
        Instance->GetCurrentTime(), Instance->GetLength());

    ImGui::SeparatorText("Camera");
    float CameraSpeed = Editor.GetCameraSpeed();
    if (ImGui::DragFloat("Camera speed", &CameraSpeed, 0.5f, 1.0f, 150.0f, "%.1f"))
    {
        Editor.SetCameraSpeed(CameraSpeed);
    }
}

void FBlendSpacePlayPanel::DrawSamples()
{
    UBlendSpace1D* BlendSpace = Editor.GetBlendSpace();
    USkeletalMeshComponent* Component = Editor.GetMeshComponent();
    if (!BlendSpace || !Component)
    {
        return;
    }

    ImGui::SeparatorText("Samples");
    if (!bAnimationListInitialized)
    {
        RefreshAnimationList();
    }
    const char* Selected = SelectedAnimation >= 0 ? AnimationPaths[SelectedAnimation].c_str() : "Select animation";
    
    if (ImGui::BeginCombo("Animation", Selected))
    {
        for (int Index = 0; Index < static_cast<int>(AnimationPaths.size()); ++Index)
        {
            if (ImGui::Selectable(AnimationPaths[Index].c_str(), SelectedAnimation == Index))
                SelectedAnimation = Index;
        }
        ImGui::EndCombo();
    }

    if (AnimationPaths.empty())
    {
        ImGui::TextUnformatted("No .anim.bin files in this BlendSpace folder.");
    }

    ImGui::InputFloat("Sample X", &SampleX);
    ImGui::BeginDisabled(SelectedAnimation < 0);
    if (ImGui::Button("Add Sample"))
    {
        SampleMessage.clear();
        UAnimSequence* Sequence = nullptr;

        if (std::isfinite(SampleX))
        {
            Sequence = UAssetManager::LoadAnimSequence(AnimationPaths[SelectedAnimation]);
        }
        else
        {
            SampleMessage = "Sample X must be finite.";
        }
        
        if (!Sequence || !Sequence->GetDataModel())
        {
            if (SampleMessage.empty())
                SampleMessage = "Failed to load animation.";
        }

        if (Sequence && Sequence->GetDataModel())
        {
            bool bCompatible = true;
            for (const FBoneAnimationTrack& Track : Sequence->GetDataModel()->GetBoneAnimationTracks())
            {
                if (Component->FindBoneIndex(Track.Name) < 0)
                {
                    bCompatible = false;
                    break;
                }
            }

            if (!bCompatible)
            {
                SampleMessage = "Animation bones do not match the preview mesh.";
            }
            else if (!BlendSpace->AddSample(Sequence, FVector(SampleX, 0.0f, 0.0f)))
            {
                SampleMessage = "A sample already exists at this position.";
            }
            else
            {
                Component->RefreshAnimationPose();
                SampleMessage = "Sample added. Press Save to keep changes.";
            }
        }
    }

    ImGui::EndDisabled();

    if (!SampleMessage.empty())
        ImGui::TextWrapped("%s", SampleMessage.c_str());

    const auto& Samples = BlendSpace->GetSampleData();
    ImGui::Text("Samples: %d", Samples.Num());
    int32 RemoveIndex = -1;
    for (int32 Index = 0; Index < Samples.Num(); ++Index)
    {
        const FBlendSample& Sample = Samples[Index];
        ImGui::PushID(Index);

        if (ImGui::SmallButton("Remove"))
        {
            RemoveIndex = Index;
        }

        ImGui::SameLine();
        ImGui::Text("X: %.2f", Sample.SampleValue.X);

        const char* Path = Sample.Animation ? Sample.Animation->GetAssetPath().c_str() : "(missing)";
        ImGui::TextWrapped("%s", Path);
        ImGui::PopID();
    }

    if (RemoveIndex >= 0)
    {
        BlendSpace->RemoveSample(RemoveIndex);
        Component->RefreshAnimationPose();
    }
}

void FBlendSpacePlayPanel::RefreshAnimationList()
{
    FString Previous;
    if (SelectedAnimation >= 0 && SelectedAnimation < static_cast<int>(AnimationPaths.size()))
    {
        Previous = AnimationPaths[SelectedAnimation];
    }

    AnimationPaths.clear();
    SelectedAnimation = -1;
    bAnimationListInitialized = true;
    SampleMessage.clear();

    FString FilePath = FAssetUtils::ResolveAssetFilePath(Editor.GetAssetPath());
    if (FilePath.empty())
    {
        FilePath = Editor.GetAssetPath();
    }
    std::filesystem::path BlendSpacePath(FilePath);
    std::filesystem::path Directory = BlendSpacePath.parent_path();

    std::error_code Error;
    std::filesystem::directory_iterator It(Directory, Error);
    std::filesystem::directory_iterator End;

    while (!Error && It != End)
    {
        bool bIsFile = It->is_regular_file(Error);
        if (bIsFile)
        {
            FString Path = It->path().generic_string();
            if (AnimSequenceFile::IsAnimSequencePath(Path))
            {
                FString AssetPath = FAssetUtils::GetAssetKey(Path);
                AnimationPaths.push_back(AssetPath);
            }
        }
        if (!Error)
        {
            It.increment(Error);
        }
    }

    std::sort(AnimationPaths.begin(), AnimationPaths.end());
    if (Error)
    {
        SampleMessage = "Cannot read animation folder: " + Error.message();
    }
}