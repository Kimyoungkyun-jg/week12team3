#include "EnginePCH.h"
#include "AnimTimelinePanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Animation/AnimNotify.h"
#include "Animation/AnimNotifyState.h"
#include "UObject/Class.h"
#include "UObject/Property.h"
#include "UObject/UObjectHash.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
    constexpr float RulerHeight = 24.0f;
    constexpr float NotifyRowHeight = 22.0f;
    constexpr float DiamondRadius = 5.0f;
    constexpr float MinStateBarWidth = 4.0f;
    constexpr float TrackHeaderWidth = 110.0f;
    constexpr float EndHandleWidth = 5.0f;   // 막대 안쪽으로 잡히는 폭
    constexpr float EndHandleSlop = 3.0f;    // 막대 바깥으로 더 잡히는 폭 (짧은 막대도 잡기 쉽게)

    const ImU32 RowColorA = IM_COL32(30, 33, 36, 255);
    const ImU32 RowColorB = IM_COL32(35, 38, 42, 255);
    const ImU32 NamedNotifyColor = IM_COL32(230, 190, 80, 255);    // 이름 Notify
    const ImU32 ClassNotifyColor = IM_COL32(90, 190, 230, 255);    // UAnimNotify
    const ImU32 NotifyStateColor = IM_COL32(170, 120, 220, 255);   // UAnimNotifyState

    const UObject* NotifyObjectOf(const FAnimNotifyEvent& Event)
    {
        if (Event.NotifyStateClass) return Event.NotifyStateClass;
        return Event.Notify;
    }

    ImU32 NotifyColorOf(const FAnimNotifyEvent& Event)
    {
        if (Event.NotifyStateClass) return NotifyStateColor;
        return Event.Notify ? ClassNotifyColor : NamedNotifyColor;
    }

    ImU32 WithAlpha(ImU32 Color, int Alpha)
    {
        return (Color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
    }

    // "UAnimNotifyState_AttackWindow" -> "AttackWindow"
    FString ShortClassName(const UObject& Object)
    {
        FString Name = Object.GetClass()->Name;
        if (!Name.empty() && Name[0] == 'U') Name.erase(0, 1);
        for (const char* Prefix : { "AnimNotifyState_", "AnimNotify_" })
        {
            if (Name.rfind(Prefix, 0) == 0)
            {
                Name.erase(0, std::strlen(Prefix));
                break;
            }
        }
        return Name;
    }

    FString NotifyLabel(const FAnimNotifyEvent& Event)
    {
        if (auto* Named = dynamic_cast<const UAnimNotifyState_Named*>(Event.NotifyStateClass))
            return Named->NotifyName.empty() ? FString("(Unnamed)") : Named->NotifyName;

        if (auto* Sound = dynamic_cast<const UAnimNotify_PlaySound*>(Event.Notify))
        {
            const FString Stem = std::filesystem::path(Sound->SoundPath).stem().string();
            return Stem.empty() ? FString("PlaySound") : "PlaySound: " + Stem;
        }

        if (const UObject* Object = NotifyObjectOf(Event))
            return ShortClassName(*Object);

        return Event.NotifyName.IsNone() ? FString("(Unnamed)") : Event.NotifyName.ToString();
    }

    // 툴팁에 Notify 객체의 UPROPERTY 값을 보여준다 (편집은 디테일 패널 단계에서)
    void DrawPropertyLines(const UObject& Object)
    {
        for (const UStruct* Struct = Object.GetClass(); Struct; Struct = Struct->GetSuperStruct())
        {
            for (const FProperty* Property : Struct->GetProperties())
            {
                const void* Value = Property->ContainerPtrToValuePtr<void>(&Object);
                const char* Name = Property->Name.c_str();
                if (CastField<FFloatProperty>(Property))       ImGui::Text("%s: %.3f", Name, *static_cast<const float*>(Value));
                else if (CastField<FIntProperty>(Property))    ImGui::Text("%s: %d", Name, *static_cast<const int32*>(Value));
                else if (CastField<FBoolProperty>(Property))   ImGui::Text("%s: %s", Name, *static_cast<const bool*>(Value) ? "true" : "false");
                else if (CastField<FStrProperty>(Property))    ImGui::Text("%s: %s", Name, static_cast<const FString*>(Value)->c_str());
                else if (CastField<FVectorProperty>(Property))
                {
                    const FVector& V = *static_cast<const FVector*>(Value);
                    ImGui::Text("%s: (%.2f, %.2f, %.2f)", Name, V.X, V.Y, V.Z);
                }
            }
        }
    }

    void DrawNotifyTooltip(const FAnimNotifyEvent& Event, float FrameRate)
    {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(NotifyColorOf(Event)), "%s", NotifyLabel(Event).c_str());

        const UObject* Object = NotifyObjectOf(Event);
        if (Event.NotifyStateClass)  ImGui::TextDisabled("NotifyState (%s)", Object->GetClass()->Name.c_str());
        else if (Event.Notify)       ImGui::TextDisabled("Notify (%s)", Object->GetClass()->Name.c_str());
        else                         ImGui::TextDisabled("Notify (Name)");

        ImGui::Separator();
        ImGui::Text("Time: %.3f s  (Frame %.1f)", Event.TriggerTime, Event.TriggerTime * FrameRate);
        if (Event.NotifyStateClass)
            ImGui::Text("Duration: %.3f s  (%.1f frames)", Event.Duration, Event.Duration * FrameRate);
        if (Event.TriggerWeightThreshold > 0.0f)
            ImGui::Text("Weight Threshold: %.2f", Event.TriggerWeightThreshold);
        ImGui::Text("Track: %d", Event.TrackIndex);

        if (Object)
        {
            ImGui::Separator();
            DrawPropertyLines(*Object);
        }
        ImGui::EndTooltip();
    }
}

void FAnimTimelinePanel::DrawContent()
{
    if (!Editor.GetSequence())
    {
        ImGui::TextDisabled("No sequence");
        return;
    }

    DrawTransport();
    ImGui::Separator();
    DrawTrack();

    // Delete 키: 타임라인 창에 포커스가 있고 글자 입력 중이 아닐 때 선택한 Notify 삭제
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput
        && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        DeleteNotify(Editor.GetSelectedNotify());
    }
}

void FAnimTimelinePanel::DrawTransport()
{
    const float Length = Editor.GetPlayLength();
    const float Rate = Editor.GetFrameRate();

    if (ImGui::Button("|<")) Editor.SetPlaybackTime(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("<")) Editor.StepFrame(-1);
    ImGui::SameLine();
    if (ImGui::Button(Editor.IsPlaying() ? "Pause" : "Play"))
    {
        if (Editor.IsPlaying()) Editor.Pause();
        else Editor.Play();
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) Editor.StepFrame(+1);
    ImGui::SameLine();
    if (ImGui::Button(">|")) Editor.SetPlaybackTime(Length);

    ImGui::SameLine();
    ImGui::Checkbox("Loop", &Editor.bLooping);
    ImGui::SameLine();
    ImGui::Checkbox("Notifies", &Editor.bPreviewNotifies);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Fire notifies while playing (sounds etc.). Name-based notifies are logged.");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Rate", &Editor.PlayRate, 0.01f, -2.0f, 2.0f, "%.2fx");

    const float Time = Editor.GetPlaybackTime();
    ImGui::SameLine();
    ImGui::Text("%.2f / %.2f s   Frame %d / %d", Time, Length,
        static_cast<int>(std::lround(Time * Rate)), Editor.GetNumFrames());

    // 저장: 변경이 있을 때만 누를 수 있다 (Ctrl+S와 같음)
    ImGui::SameLine(0.0f, 24.0f);
    const bool bDirty = Editor.IsDirty();
    ImGui::BeginDisabled(!bDirty);
    if (ImGui::Button("Save"))
    {
        Editor.Save();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Save notifies and root motion settings to the .anim.bin (Ctrl+S)");
    }

    ImGui::SameLine();
    if (bDirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Unsaved changes");
    }
    else if (!Editor.GetSaveMessage().empty())
    {
        const bool bFailed = Editor.GetSaveMessage().rfind("failed") != FString::npos;
        ImGui::TextColored(bFailed ? ImVec4(1.0f, 0.4f, 0.3f, 1.0f) : ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%s", Editor.GetSaveMessage().c_str());
    }
}

void FAnimTimelinePanel::DrawTrack()
{
    const float Length = Editor.GetPlayLength();
    const float Rate = Editor.GetFrameRate();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImGuiIO& IO = ImGui::GetIO();
    const ImVec2 PanelOrigin = ImGui::GetCursorScreenPos();

    // 왼쪽 열: 트랙 이름. 눈금자 줄의 왼쪽 칸에는 트랙 추가 버튼.
    ImGui::SetCursorScreenPos(ImVec2(PanelOrigin.x + 4.0f, PanelOrigin.y + (RulerHeight - ImGui::GetFrameHeight()) * 0.5f));
    if (ImGui::SmallButton("+ Track"))
    {
        Editor.GetSequence()->AddNotifyTrack();
        Editor.MarkNotifiesDirty();
    }

    // 오른쪽: 시간축 (눈금자와 Notify 트랙이 같은 X축을 쓴다)
    const ImVec2 Origin(PanelOrigin.x + TrackHeaderWidth, PanelOrigin.y);
    const float Width = std::max(ImGui::GetContentRegionAvail().x - TrackHeaderWidth, 50.0f);
    const float PixelsPerSecond = Length > 0.0f ? Width / Length : 1.0f;   // 클립 전체를 한 화면에
    ImGui::SetCursorScreenPos(Origin);

    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + RulerHeight), IM_COL32(40, 44, 48, 255));

    // 눈금: 60픽셀 이상 간격이 나오는 가장 작은 단위를 고른다 (SequencerPanel과 같은 방식)
    const float Steps[] = { 1.0f / Rate, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f };
    float Step = Steps[IM_ARRAYSIZE(Steps) - 1];
    for (float Candidate : Steps)
    {
        if (Candidate * PixelsPerSecond >= 60.0f) { Step = Candidate; break; }
    }
    for (float T = 0.0f; T <= Length + 0.0001f; T += Step)
    {
        const float X = Origin.x + T * PixelsPerSecond;
        Draw->AddLine(ImVec2(X, Origin.y + RulerHeight - 6.0f), ImVec2(X, Origin.y + RulerHeight), IM_COL32(150, 150, 150, 255));
        char Label[16];
        std::snprintf(Label, sizeof(Label), "%.2f", T);
        Draw->AddText(ImVec2(X + 3.0f, Origin.y + 3.0f), IM_COL32(190, 190, 190, 255), Label);
    }

    // 스크럽: 누른 동안 마우스를 잡아 두므로 영역 밖으로 끌어도 계속된다
    ImGui::InvisibleButton("##Scrub", ImVec2(Width, RulerHeight));
    if (ImGui::IsItemActivated())
    {
        bWasPlayingBeforeScrub = Editor.IsPlaying();
    }
    if (ImGui::IsItemActive())
    {
        Editor.SetPlaybackTime(std::clamp((IO.MousePos.x - Origin.x) / PixelsPerSecond, 0.0f, Length));
    }
    if (ImGui::IsItemDeactivated() && bWasPlayingBeforeScrub)
    {
        Editor.Play();
    }

    // Notify 트랙 (눈금자와 같은 X축). 왼쪽 열에 트랙 이름.
    const ImVec2 TracksOrigin(Origin.x, ImGui::GetCursorScreenPos().y);
    const float TracksHeight = DrawNotifyTracks(TracksOrigin, Width, PixelsPerSecond);
    DrawTrackHeaders(ImVec2(PanelOrigin.x, TracksOrigin.y));
    ImGui::SetCursorScreenPos(ImVec2(PanelOrigin.x, TracksOrigin.y + TracksHeight));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));   // 커서를 트랙 아래로 옮긴 뒤 레이아웃을 확정한다

    // 플레이헤드: 눈금자부터 트랙 끝까지
    const float PlayheadX = Origin.x + Editor.GetPlaybackTime() * PixelsPerSecond;
    Draw->AddLine(ImVec2(PlayheadX, Origin.y), ImVec2(PlayheadX, TracksOrigin.y + TracksHeight), IM_COL32(255, 80, 60, 255), 2.0f);
}

void FAnimTimelinePanel::DrawTrackHeaders(const ImVec2& HeaderOrigin)
{
    UAnimSequence* Sequence = Editor.GetSequence();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const int32 NumTracks = Sequence->GetNumNotifyTracks();
    const ImVec2 CellSize(TrackHeaderWidth - 2.0f, NotifyRowHeight);

    bool bOpenMenu = false;
    bool bOpenRename = false;

    for (int32 Track = 0; Track < NumTracks; ++Track)
    {
        const ImVec2 Min(HeaderOrigin.x, HeaderOrigin.y + Track * NotifyRowHeight);
        const ImVec2 Max(Min.x + CellSize.x, Min.y + CellSize.y);
        Draw->AddRectFilled(Min, Max, (Track % 2) ? RowColorB : RowColorA);

        // 긴 이름은 칸 안에서 자른다
        Draw->PushClipRect(Min, Max, true);
        Draw->AddText(ImVec2(Min.x + 6.0f, Min.y + (NotifyRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
            IM_COL32(200, 200, 200, 255), Sequence->GetNotifyTrackName(Track).c_str());
        Draw->PopClipRect();

        ImGui::PushID(Track);
        ImGui::SetCursorScreenPos(Min);
        ImGui::InvisibleButton("##TrackHeader", CellSize);
        if (ImGui::IsItemHovered())
        {
            Draw->AddRect(Min, Max, IM_COL32(255, 255, 255, 50));
            ImGui::SetTooltip("Right-click: track menu\nDouble-click: rename");
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            ContextTrack = Track;
            bOpenMenu = true;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            ContextTrack = Track;
            bOpenRename = true;
        }
        ImGui::PopID();
    }

    // 팝업은 PushID 밖에서 열어야 아래 BeginPopup과 ID가 맞는다
    if (bOpenMenu)
    {
        ImGui::OpenPopup("##NotifyTrackMenu");
    }

    if (ImGui::BeginPopup("##NotifyTrackMenu"))
    {
        if (ContextTrack >= 0 && ContextTrack < NumTracks)
        {
            ImGui::TextDisabled("Track: %s", Sequence->GetNotifyTrackName(ContextTrack).c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Insert Track Above"))
            {
                Sequence->InsertNotifyTrack(ContextTrack);
                Editor.MarkNotifiesDirty();
            }
            if (ImGui::MenuItem("Insert Track Below"))
            {
                Sequence->InsertNotifyTrack(ContextTrack + 1);
                Editor.MarkNotifiesDirty();
            }
            if (ImGui::MenuItem("Rename..."))
            {
                bOpenRename = true;
            }

            int32 NumOnTrack = 0;
            for (const FAnimNotifyEvent& Event : Sequence->GetNotifies())
                if (Event.TrackIndex == ContextTrack) ++NumOnTrack;

            char RemoveLabel[64];
            if (NumOnTrack > 0) std::snprintf(RemoveLabel, sizeof(RemoveLabel), "Remove Track (deletes %d notifies)", NumOnTrack);
            else                std::snprintf(RemoveLabel, sizeof(RemoveLabel), "Remove Track");

            // 마지막 트랙은 지울 수 없다
            if (ImGui::MenuItem(RemoveLabel, nullptr, false, NumTracks > 1))
            {
                Sequence->RemoveNotifyTrack(ContextTrack);
                Editor.MarkNotifiesDirty();
            }
        }
        ImGui::EndPopup();
    }

    if (bOpenRename && ContextTrack >= 0 && ContextTrack < NumTracks)
    {
        const FAnimNotifyTrack* Data = ContextTrack < Sequence->GetNotifyTracks().Num() ? &Sequence->GetNotifyTracks()[ContextTrack] : nullptr;
        std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s", Data ? Data->Name.c_str() : "");
        ImGui::OpenPopup("##RenameNotifyTrack");
    }

    if (ImGui::BeginPopup("##RenameNotifyTrack"))
    {
        ImGui::Text("Track name (empty = number)");
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        const bool bEnter = ImGui::InputText("##TrackName", RenameBuffer, sizeof(RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        if (bEnter || ImGui::Button("OK"))
        {
            Sequence->RenameNotifyTrack(ContextTrack, RenameBuffer);
            Editor.MarkNotifiesDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

float FAnimTimelinePanel::DrawNotifyTracks(const ImVec2& TracksOrigin, float Width, float PixelsPerSecond)
{
    const UAnimSequence* Sequence = Editor.GetSequence();
    const TArray<FAnimNotifyEvent>& Notifies = Sequence->GetNotifies();
    const float Rate = Editor.GetFrameRate();
    const float Length = Editor.GetPlayLength();

    const int32 NumRows = Sequence->GetNumNotifyTracks();
    const float Height = NumRows * NotifyRowHeight;
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 TracksMax(TracksOrigin.x + Width, TracksOrigin.y + Height);

    // 드래그 중이면 놓일 줄을 밝게 보여 준다
    int32 DropRow = INDEX_NONE;
    if (DraggingNotify.IsValid() && bDragMoved)
        if (const FAnimNotifyEvent* Dragged = Sequence->FindNotify(DraggingNotify))
            DropRow = Dragged->TrackIndex;

    // 줄 배경
    for (int32 Row = 0; Row < NumRows; ++Row)
    {
        const float Y = TracksOrigin.y + Row * NotifyRowHeight;
        const ImU32 RowColor = (Row == DropRow) ? IM_COL32(55, 62, 72, 255) : ((Row % 2) ? RowColorB : RowColorA);
        Draw->AddRectFilled(ImVec2(TracksOrigin.x, Y), ImVec2(TracksMax.x, Y + NotifyRowHeight), RowColor);
    }

    // 프레임 격자: 프레임 간격이 충분히 넓을 때만
    const float PixelsPerFrame = Rate > 0.0f ? PixelsPerSecond / Rate : 0.0f;
    if (PixelsPerFrame >= 6.0f)
    {
        const int32 NumFrames = static_cast<int32>(std::floor(Length * Rate + 0.0001f));
        for (int32 Frame = 0; Frame <= NumFrames; ++Frame)
        {
            const float X = TracksOrigin.x + Frame * PixelsPerFrame;
            Draw->AddLine(ImVec2(X, TracksOrigin.y), ImVec2(X, TracksMax.y), IM_COL32(255, 255, 255, 14));
        }
    }

    // 툴팁/선택은 스크럽 같은 다른 조작 중에는 받지 않는다
    const ImVec2 Mouse = ImGui::GetIO().MousePos;
    const bool bCanHover = ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()
        && ImGui::IsMouseHoveringRect(TracksOrigin, TracksMax);
    const FAnimNotifyEvent* Hovered = nullptr;
    bool bHoveredEndHandle = false;   // NotifyState 막대 오른쪽 끝 (길이 조절)
    const FGuid& Selected = Editor.GetSelectedNotify();
    const ImU32 SelectedOutline = IM_COL32(255, 255, 255, 255);

    // State 막대를 먼저 그리고 단발 마름모를 위에 그린다 (짧은 마름모가 막대에 가려지지 않게)
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        for (const FAnimNotifyEvent& Event : Notifies)
        {
            const bool bIsState = Event.NotifyStateClass != nullptr;
            if (bIsState != (Pass == 0))
                continue;

            const float RowTop = TracksOrigin.y + Event.TrackIndex * NotifyRowHeight;
            const float CenterY = RowTop + NotifyRowHeight * 0.5f;
            const float StartX = TracksOrigin.x + Event.TriggerTime * PixelsPerSecond;
            const ImU32 Color = NotifyColorOf(Event);
            const FString Label = NotifyLabel(Event);
            const bool bSelected = Selected.IsValid() && Event.Guid == Selected;

            ImVec2 HitMin, HitMax;
            if (bIsState)
            {
                const float EndX = std::max(TracksOrigin.x + Event.GetEndTriggerTime() * PixelsPerSecond, StartX + MinStateBarWidth);
                const ImVec2 BarMin(StartX, RowTop + 3.0f);
                const ImVec2 BarMax(EndX, RowTop + NotifyRowHeight - 3.0f);
                Draw->AddRectFilled(BarMin, BarMax, WithAlpha(Color, bSelected ? 210 : 150), 3.0f);
                Draw->AddRect(BarMin, BarMax, bSelected ? SelectedOutline : Color, 3.0f, 0, bSelected ? 2.0f : 1.0f);

                // 라벨은 막대 안에서 잘라 그린다
                Draw->PushClipRect(BarMin, BarMax, true);
                Draw->AddText(ImVec2(BarMin.x + 4.0f, CenterY - ImGui::GetTextLineHeight() * 0.5f), IM_COL32(255, 255, 255, 230), Label.c_str());
                Draw->PopClipRect();

                // 선택되면 끝 핸들을 보여 준다
                if (bSelected)
                    Draw->AddRectFilled(ImVec2(BarMax.x - 3.0f, BarMin.y + 2.0f), ImVec2(BarMax.x - 1.0f, BarMax.y - 2.0f), SelectedOutline);

                HitMin = BarMin;
                HitMax = ImVec2(BarMax.x + EndHandleSlop, BarMax.y);
            }
            else
            {
                const float Radius = bSelected ? DiamondRadius + 1.5f : DiamondRadius;
                const ImVec2 Points[4] =
                {
                    ImVec2(StartX, CenterY - Radius),
                    ImVec2(StartX + Radius, CenterY),
                    ImVec2(StartX, CenterY + Radius),
                    ImVec2(StartX - Radius, CenterY),
                };
                Draw->AddConvexPolyFilled(Points, 4, Color);
                Draw->AddPolyline(Points, 4, bSelected ? SelectedOutline : IM_COL32(20, 20, 20, 255), ImDrawFlags_Closed, bSelected ? 2.0f : 1.0f);
                Draw->AddText(ImVec2(StartX + DiamondRadius + 3.0f, CenterY - ImGui::GetTextLineHeight() * 0.5f), IM_COL32(220, 220, 220, 255), Label.c_str());

                HitMin = ImVec2(StartX - DiamondRadius - 1.0f, RowTop);
                HitMax = ImVec2(StartX + DiamondRadius + 1.0f, RowTop + NotifyRowHeight);
            }

            // 미리보기에서 방금 발동했으면 잠깐 빛난다
            if (const float Flash = Editor.GetNotifyFlash(Event.Guid); Flash > 0.0f)
            {
                const int Alpha = static_cast<int>(Flash * 220.0f);
                if (bIsState)
                    Draw->AddRect(ImVec2(HitMin.x - 2.0f, HitMin.y - 2.0f), ImVec2(HitMax.x - EndHandleSlop + 2.0f, HitMax.y + 2.0f), IM_COL32(255, 255, 160, Alpha), 4.0f, 0, 2.0f);
                else
                    Draw->AddCircle(ImVec2(StartX, CenterY), DiamondRadius + 4.0f + (1.0f - Flash) * 4.0f, IM_COL32(255, 255, 160, Alpha), 0, 2.0f);
            }

            // 나중에 그린 것(위에 보이는 것)이 우선
            if (bCanHover && Mouse.x >= HitMin.x && Mouse.x <= HitMax.x && Mouse.y >= HitMin.y && Mouse.y <= HitMax.y)
            {
                Hovered = &Event;
                bHoveredEndHandle = bIsState && Mouse.x >= HitMax.x - EndHandleSlop - EndHandleWidth;
            }
        }
    }

    // 끝 핸들 위에서는 좌우 화살표 커서
    if (bHoveredEndHandle || (DraggingNotify.IsValid() && bDragResize && bDragMoved))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // 트랙 영역 전체를 버튼으로 덮어 클릭/드래그를 받는다 (레이아웃 공간도 차지해 창 스크롤이 생긴다)
    ImGui::SetCursorScreenPos(TracksOrigin);
    ImGui::InvisibleButton("##NotifyTracks", ImVec2(Width, Height));

    // 툴팁은 편집 전에 띄운다 (편집하면 배열이 재정렬돼 Hovered 포인터가 무효가 될 수 있다)
    if (Hovered && ImGui::IsItemHovered() && !DraggingNotify.IsValid())
    {
        DrawNotifyTooltip(*Hovered, Rate);
    }

    if (ImGui::IsItemActivated())
    {
        // Notify 위에서 누르면 선택 + 드래그 시작, 빈 곳이면 선택 해제
        if (Hovered)
        {
            Editor.SelectNotify(Hovered->Guid);
            DraggingNotify = Hovered->Guid;
            DragGrabOffset = (Mouse.x - TracksOrigin.x) / PixelsPerSecond - Hovered->TriggerTime;
            DragStartMouse = Mouse;
            bDragMoved = false;
            bDragResize = bHoveredEndHandle;
        }
        else
        {
            Editor.ClearNotifySelection();
        }
    }

    // 우클릭: 그 자리(트랙, 프레임에 맞춘 시간)에 추가하는 메뉴. Notify 위면 삭제도.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !DraggingNotify.IsValid())
    {
        const float RawTime = std::clamp((Mouse.x - TracksOrigin.x) / PixelsPerSecond, 0.0f, Length);
        ContextTime = Rate > 0.0f ? std::round(RawTime * Rate) / Rate : RawTime;
        ContextNotifyTrack = std::clamp(static_cast<int32>((Mouse.y - TracksOrigin.y) / NotifyRowHeight), 0, NumRows - 1);
        ContextNotify = Hovered ? Hovered->Guid : FGuid();
        if (Hovered) Editor.SelectNotify(Hovered->Guid);
        ImGui::OpenPopup("##NotifyContextMenu");
    }
    DrawNotifyContextMenu();

    if (ImGui::IsItemActive() && DraggingNotify.IsValid())
    {
        UpdateNotifyDrag(TracksOrigin, PixelsPerSecond);
    }

    if (ImGui::IsItemDeactivated())
    {
        DraggingNotify.Invalidate();
        bDragMoved = false;
    }

    return Height;
}

void FAnimTimelinePanel::DrawNotifyContextMenu()
{
    if (!ImGui::BeginPopup("##NotifyContextMenu"))
    {
        return;
    }

    UAnimSequence* Sequence = Editor.GetSequence();
    const float Rate = Editor.GetFrameRate();

    // 우클릭한 Notify가 있으면 맨 위에 삭제
    if (ContextNotify.IsValid())
    {
        if (const FAnimNotifyEvent* Event = Sequence->FindNotify(ContextNotify))
        {
            ImGui::TextDisabled("%s", NotifyLabel(*Event).c_str());
            if (ImGui::MenuItem("Delete", "Del"))
            {
                DeleteNotify(ContextNotify);
            }
            ImGui::Separator();
        }
    }

    ImGui::TextDisabled("Track %s  |  %.3f s (Frame %d)", Sequence->GetNotifyTrackName(ContextNotifyTrack).c_str(),
        ContextTime, static_cast<int>(std::lround(ContextTime * Rate)));

    // 새 Notify를 우클릭한 트랙에 두고 선택한다
    auto Finish = [&](const FGuid& Guid)
        {
            Sequence->SetNotifyTrackIndex(Guid, ContextNotifyTrack);
            Editor.SelectNotify(Guid);
            Editor.MarkNotifiesDirty();
        };
    // NotifyState 기본 길이: 5프레임 (시퀀스 끝을 넘으면 엔진이 자른다)
    const float DefaultDuration = Rate > 0.0f ? 5.0f / Rate : 0.1f;

    if (ImGui::BeginMenu("Add Notify"))
    {
        if (ImGui::MenuItem("Named Notify"))
        {
            Finish(Sequence->AddNotify(ContextTime, FName("NewNotify")));
        }
        ImGui::Separator();

        TArray<UClass*> Classes;
        GetDerivedClasses(UAnimNotify::StaticClass(), Classes, true);
        std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
        for (UClass* Class : Classes)
        {
            if (ImGui::MenuItem(Class->Name.c_str()))
            {
                FGuid Guid;
                if (Sequence->AddNotifyOfClass(Class, ContextTime, 0.0f, &Guid))
                    Finish(Guid);
            }
        }
        if (Classes.IsEmpty())
        {
            ImGui::TextDisabled("(no UAnimNotify classes)");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add Notify State"))
    {
        if (ImGui::MenuItem("Named Notify State"))
        {
            FGuid Guid;
            Sequence->AddNotify<UAnimNotifyState_Named>(ContextTime, DefaultDuration, &Guid)->NotifyName = "NewNotifyState";
            Finish(Guid);
        }
        ImGui::Separator();

        TArray<UClass*> Classes;
        GetDerivedClasses(UAnimNotifyState::StaticClass(), Classes, true);
        std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
        for (UClass* Class : Classes)
        {
            if (Class == UAnimNotifyState_Named::StaticClass())
                continue;   // 위의 "Named Notify State"로 따로 보여 준다
            if (ImGui::MenuItem(Class->Name.c_str()))
            {
                FGuid Guid;
                if (Sequence->AddNotifyOfClass(Class, ContextTime, DefaultDuration, &Guid))
                    Finish(Guid);
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndPopup();
}

void FAnimTimelinePanel::DeleteNotify(const FGuid& Guid)
{
    if (!Guid.IsValid())
        return;

    // 드래그 중인 Notify를 지우면 드래그도 끝낸다
    if (DraggingNotify == Guid)
        DraggingNotify.Invalidate();

    if (Editor.GetSequence()->RemoveNotify(Guid))
    {
        if (Editor.GetSelectedNotify() == Guid)
            Editor.ClearNotifySelection();
        Editor.MarkNotifiesDirty();
    }
}

void FAnimTimelinePanel::UpdateNotifyDrag(const ImVec2& TracksOrigin, float PixelsPerSecond)
{
    UAnimSequence* Sequence = Editor.GetSequence();
    const FAnimNotifyEvent* Event = Sequence->FindNotify(DraggingNotify);
    if (!Event)
    {
        DraggingNotify.Invalidate();
        return;
    }

    const ImVec2 Mouse = ImGui::GetIO().MousePos;

    // 클릭만 했을 때 1~2픽셀 흔들림으로 움직이지 않도록 문턱을 둔다
    if (!bDragMoved)
    {
        const float Dx = Mouse.x - DragStartMouse.x;
        const float Dy = Mouse.y - DragStartMouse.y;
        if (Dx * Dx + Dy * Dy < 4.0f * 4.0f)
            return;
        bDragMoved = true;
    }

    const float Rate = Editor.GetFrameRate();
    const float Length = Editor.GetPlayLength();
    const bool bSnap = Rate > 0.0f && !ImGui::GetIO().KeyShift;

    // 끝 핸들: 시작은 그대로 두고 길이만 바꾼다 (최소 1프레임)
    if (bDragResize)
    {
        float EndTime = std::clamp((Mouse.x - TracksOrigin.x) / PixelsPerSecond, 0.0f, Length);
        if (bSnap)
            EndTime = std::round(EndTime * Rate) / Rate;
        const float MinDuration = Rate > 0.0f ? 1.0f / Rate : 0.01f;
        const float NewDuration = std::max(EndTime - Event->TriggerTime, MinDuration);
        if (std::fabs(NewDuration - Event->Duration) > 1e-6f)
        {
            Sequence->SetNotifyDuration(DraggingNotify, NewDuration);
            Editor.MarkNotifiesDirty();
        }
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // 세로: 마우스가 있는 줄로 트랙 이동
    const int32 NumTracks = Sequence->GetNumNotifyTracks();
    const int32 TargetTrack = std::clamp(static_cast<int32>(std::floor((Mouse.y - TracksOrigin.y) / NotifyRowHeight)), 0, NumTracks - 1);

    // 가로: 시간 이동. 기본은 프레임 단위로 맞추고, Shift를 누르면 자유롭게.
    float TargetTime = (Mouse.x - TracksOrigin.x) / PixelsPerSecond - DragGrabOffset;
    if (bSnap)
        TargetTime = std::round(TargetTime * Rate) / Rate;
    // NotifyState는 끝이 시퀀스 밖으로 나가면 길이가 잘리므로, 길이를 지키도록 시작 시간을 제한한다
    const float MaxStart = std::max(Length - (Event->NotifyStateClass ? Event->Duration : 0.0f), 0.0f);
    TargetTime = std::clamp(TargetTime, 0.0f, MaxStart);

    const bool bTrackChanged = TargetTrack != Event->TrackIndex;
    const bool bTimeChanged = std::fabs(TargetTime - Event->TriggerTime) > 1e-6f;

    // SetNotifyTriggerTime은 재정렬하므로 Event 포인터는 여기서부터 쓰지 않는다
    if (bTrackChanged) Sequence->SetNotifyTrackIndex(DraggingNotify, TargetTrack);
    if (bTimeChanged)  Sequence->SetNotifyTriggerTime(DraggingNotify, TargetTime);
    if (bTrackChanged || bTimeChanged)
        Editor.MarkNotifiesDirty();
}
