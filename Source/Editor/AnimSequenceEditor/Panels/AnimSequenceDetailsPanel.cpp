#include "EnginePCH.h"
#include "AnimSequenceDetailsPanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Editor/Details/PropertyEditor.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotify.h"
#include "Animation/AnimNotifyState.h"
#include "Animation/AnimSequenceFile.h"
#include "Asset/AssetUtils.h"
#include "Render/SkeletalMesh.h"

#include <cstdio>

void FAnimSequenceDetailsPanel::DrawContent()
{
    UAnimSequence* Sequence = Editor.GetSequence();
    const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
    if (!Model)
    {
        return;
    }

    // 타임라인에서 Notify를 고르면 맨 위에 편집 칸을 보여 준다
    if (Editor.GetSelectedNotifyEvent())
    {
        DrawNotifySection(*Sequence);
        ImGui::Spacing();
    }

    ImGui::TextDisabled("Asset Path");
    ImGui::TextWrapped("%s", Editor.GetAssetPath().c_str());
    ImGui::Separator();

    ImGui::Text("Clip: %s", Sequence->GetClipName().c_str());
    ImGui::Text("Length: %.3f s", Model->GetPlayLength());
    ImGui::Text("Frame Rate: %.2f", Model->GetFrameRate().AsDecimal());
    ImGui::Text("Frames: %d (keys %d)", Model->GetNumberOfFrames(), Model->GetNumberOfKeys());
    ImGui::Text("Tracks: %d", static_cast<int>(Model->GetBoneAnimationTracks().Num()));
    ImGui::Text("Notifies: %d", static_cast<int>(Sequence->GetNotifies().Num()));

    // ── 에셋 설정: 시퀀스에 저장되고 게임에도 적용된다 ──
    ImGui::SeparatorText("Root Motion");
    bool bEnableRootMotion = Sequence->GetEnableRootMotion();
    if (ImGui::Checkbox("Enable Root Motion", &bEnableRootMotion))
    {
        Sequence->SetEnableRootMotion(bEnableRootMotion);
        Editor.MarkPoseDirty();   // 일시정지 중에도 포즈를 다시 계산해 root 고정을 바로 보여준다
    }

    // root를 고정할 기준. 일시정지 중에도 변경된 기준으로 포즈를 갱신한다.
    static const char* RootLockNames[] = { "Ref Pose", "Anim First Frame", "Zero" };
    int RootLock = static_cast<int>(Sequence->GetRootMotionRootLock());
    if (ImGui::Combo("Root Motion Root Lock", &RootLock, RootLockNames, IM_ARRAYSIZE(RootLockNames)))
    {
        Sequence->SetRootMotionRootLock(static_cast<ERootMotionRootLock>(RootLock));
        Editor.MarkPoseDirty();
    }

    // 루트 모션이 꺼져 있어도 root를 고정한다 (Actor는 움직이지 않는 제자리 재생)
    bool bForceRootLock = Sequence->GetForceRootLock();
    if (ImGui::Checkbox("Force Root Lock", &bForceRootLock))
    {
        Sequence->SetForceRootLock(bForceRootLock);
        Editor.MarkPoseDirty();
    }

    // 바꾸면 Unsaved changes로 표시되고, 타임라인의 Save(Ctrl+S)로 .anim.bin에 저장한다.
    // 저장 전에도 같은 시퀀스를 쓰는 미리보기·게임에 바로 적용된다 (AssetManager 캐시 공유)

    ImGui::SeparatorText("Preview Mesh");
    ImGui::TextWrapped("%s", Sequence->GetPreviewMeshPath().c_str());
    if (USkeletalMesh* Mesh = Editor.GetMesh())
    {
        ImGui::Text("Bones: %d", Mesh->GetNumBones());
    }
}

void FAnimSequenceDetailsPanel::DrawNotifySection(UAnimSequence& Sequence)
{
    // 편집 함수(SetNotifyTriggerTime 등)는 배열을 다시 정렬해 원소 주소가 바뀐다.
    // 값은 먼저 지역 변수로 복사하고, 바꿀 때는 Guid로만 부른다. 객체 포인터는 정렬과 무관하게 그대로다.
    const FAnimNotifyEvent& Event = *Editor.GetSelectedNotifyEvent();
    const FGuid Guid = Event.Guid;
    UObject* NotifyObject = Event.NotifyStateClass ? static_cast<UObject*>(Event.NotifyStateClass) : static_cast<UObject*>(Event.Notify);
    const bool bIsState = Event.NotifyStateClass != nullptr;
    const bool bIsNamed = NotifyObject == nullptr;

    float Time = Event.TriggerTime;
    float Duration = Event.Duration;
    float Threshold = Event.TriggerWeightThreshold;
    int TrackIndex = Event.TrackIndex;
    const FString Name = Event.NotifyName.IsNone() ? FString() : Event.NotifyName.ToString();

    const float Length = Editor.GetPlayLength();
    const float Rate = Editor.GetFrameRate();
    const float NumFrames = static_cast<float>(Editor.GetNumFrames());

    ImGui::SeparatorText(bIsState ? "Notify State" : "Notify");
    if (bIsNamed) ImGui::TextDisabled("Name-based (HandleNamedNotify)");
    else          ImGui::TextDisabled("%s", NotifyObject->GetClass()->Name.c_str());

    bool bChanged = false;
    ImGui::PushID("NotifyEdit");

    if (bIsNamed)
    {
        // 입력 중에는 매 글자마다 바꾸지 않고, 편집을 마쳤을 때 한 번 반영한다
        char Buffer[128];
        std::snprintf(Buffer, sizeof(Buffer), "%s", Name.c_str());
        ImGui::InputText("Name", Buffer, sizeof(Buffer));
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Sequence.SetNotifyName(Guid, Buffer[0] ? FName(FString(Buffer)) : FName());
            bChanged = true;
        }
    }

    if (ImGui::DragFloat("Time (s)", &Time, 0.001f, 0.0f, Length, "%.3f"))
    {
        Sequence.SetNotifyTriggerTime(Guid, Time);
        bChanged = true;
    }

    float Frame = Time * Rate;
    if (Rate > 0.0f && ImGui::DragFloat("Frame", &Frame, 0.05f, 0.0f, NumFrames, "%.2f"))
    {
        Sequence.SetNotifyTriggerTime(Guid, Frame / Rate);
        bChanged = true;
    }

    if (bIsState)
    {
        if (ImGui::DragFloat("Duration (s)", &Duration, 0.001f, 0.0f, Length, "%.3f"))
        {
            Sequence.SetNotifyDuration(Guid, Duration);
            bChanged = true;
        }
        float DurationFrames = Duration * Rate;
        if (Rate > 0.0f && ImGui::DragFloat("Duration (frames)", &DurationFrames, 0.05f, 0.0f, NumFrames, "%.2f"))
        {
            Sequence.SetNotifyDuration(Guid, DurationFrames / Rate);
            bChanged = true;
        }
    }

    if (ImGui::SliderFloat("Weight Threshold", &Threshold, 0.0f, 1.0f, "%.2f"))
    {
        Sequence.SetNotifyTriggerWeightThreshold(Guid, Threshold);
        bChanged = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Fires only when the clip's blend weight is at least this value.\nUse 0.5 in BlendSpaces so only the dominant sample fires.");
    }

    if (ImGui::InputInt("Track", &TrackIndex))
    {
        Sequence.SetNotifyTrackIndex(Guid, TrackIndex);
        bChanged = true;
    }

    // Notify 객체의 UPROPERTY(EditAnywhere)는 레벨 디테일 패널과 같은 위젯으로 편집한다
    if (NotifyObject)
    {
        ImGui::BeginGroup();
        FPropertyEditor::DrawObjectProperties(NotifyObject);
        ImGui::EndGroup();
        if (ImGui::IsItemEdited())
            bChanged = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("Delete Notify"))
    {
        Sequence.RemoveNotify(Guid);
        Editor.ClearNotifySelection();
        bChanged = true;
    }

    ImGui::PopID();

    if (bChanged)
    {
        Editor.MarkNotifiesDirty();
    }
}
