#include "EnginePCH.h"
#include "Editor/Outliner/OutlinerPanel.h"

#include "Engine/Level.h"
#include "Engine/World.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/UObjectGlobals.h"
#include "Editor/HitoriEd/EditorSelection.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/EditorUI/EditorUIScale.h"

bool FOutlinerPanel::Init()
{
    return true;
}

void FOutlinerPanel::Tick(float DeltaTime)
{
}

void FOutlinerPanel::OnRender()
{
    // World 참조 확인
    if (!World)
        return;

    // 현재 Level 가져오기
    ULevel* Level = World->GetCurrentLevel();
    if (!Level)
        return;

    // Level의 Actor 목록 Tree 출력
    if (!BeginPanel())
    {
        ImGui::End();
        return;
    }

    DrawActors(Level);

    ImGui::End();

    // 목록을 다 그린 뒤 계층을 바꾼다. 월드 위치는 그대로 두고 부모만 바꾼다 (UE와 같음).
    if (bPendingAttach)
    {
        if (PendingAttachActor && PendingAttachActor != PendingAttachParent)
        {
            if (!PendingAttachParent)
                PendingAttachActor->DetachFromActor(EAttachmentRule::KeepWorld);
            else if (!PendingAttachActor->AttachToActor(PendingAttachParent, EAttachmentRule::KeepWorld))
                HTR_LOG(Warning, "Cannot attach {} to {}", PendingAttachActor->GetName(), PendingAttachParent->GetName());
        }
        PendingAttachActor = nullptr;
        PendingAttachParent = nullptr;
        bPendingAttach = false;
    }

    // Actors 순회가 완전히 끝난 뒤 삭제
    if (PendingDeleteActor)
    {
        CollapsedActors.erase(PendingDeleteActor);
        if (RenamingActor == PendingDeleteActor)
            RenamingActor = nullptr;

        if (DeleteCallback)
            DeleteCallback(PendingDeleteActor);

        PendingDeleteActor = nullptr;
    }
}

// Level이 가지고 있는 Actor 목록 순회하며 Draw TreeNode
void FOutlinerPanel::DrawActors(ULevel* Level)
{
    if (!Level)
        return;

    // 부모가 없는(또는 다른 레벨에 붙은) 액터부터 자식을 펼쳐 줄 목록을 만든다.
    Rows.Reset();
    for (AActor* Actor : Level->GetActors())
    {
        if (!Actor)
            continue;
        AActor* ParentActor = Actor->GetAttachParentActor();
        if (!ParentActor || ParentActor->GetLevel() != Level)
            AddActorRows(Actor, 0, Level);
    }

    // 보이는 줄만 그린다. 이름 입력칸도 같은 줄 높이를 쓰므로 클리퍼가 어긋나지 않는다.
    ImGuiListClipper Clipper;
    Clipper.Begin(Rows.Num(), ImGui::GetTextLineHeightWithSpacing());
    while (Clipper.Step())
    {
        for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
            DrawActorNode(Rows[Index]);
    }

    // 목록 아래 빈 곳에 놓으면 부모에서 뗀다.
    const ImVec2 Remaining = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(std::max(Remaining.x, 1.0f), std::max(Remaining.y, ImGui::GetTextLineHeight())));
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
        {
            PendingAttachActor = *static_cast<AActor**>(Payload->Data);
            PendingAttachParent = nullptr;
            bPendingAttach = true;
        }
        ImGui::EndDragDropTarget();
    }
}

void FOutlinerPanel::AddActorRows(AActor* Actor, int32 Depth, ULevel* Level)
{
    TArray<AActor*> Children;
    Actor->GetAttachedActors(Children);
    Rows.Add({ Actor, Depth, !Children.IsEmpty() });
    if (CollapsedActors.contains(Actor))
        return;
    for (AActor* Child : Children)
    {
        if (Child && Child->GetLevel() == Level)
            AddActorRows(Child, Depth + 1, Level);
    }
}

void FOutlinerPanel::BeginActorRename(AActor* Actor)
{
    RenamingActor = Actor;
    strncpy_s(ActorRenameBuffer, sizeof(ActorRenameBuffer), Actor->GetName().c_str(), _TRUNCATE);
    bFocusActorRename = true;
}

void FOutlinerPanel::DrawActorRenameField(AActor* Actor)
{
    ImGui::SameLine();
    if (bFocusActorRename)
    {
        ImGui::SetKeyboardFocusHere();
        bFocusActorRename = false;
    }

    // 입력칸 안쪽 여백. 세로는 줄 높이(클리퍼 가정)를 크게 벗어나지 않게 조금만 준다.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(6.0f), EditorUIScale::Px(2.0f)));
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool bConfirmed = ImGui::InputText("##RenameActor", ActorRenameBuffer, sizeof(ActorRenameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool bDeactivated = ImGui::IsItemDeactivated();
    ImGui::PopStyleVar();

    const bool bCancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (!bCancelled && !bConfirmed && !bDeactivated)
        return;

    const FName NewName(ActorRenameBuffer);
    if (!bCancelled && ActorRenameBuffer[0] != '\0' && NewName != NAME_None && NewName != Actor->GetFName())
    {
        // 같은 Outer의 같은 이름은 오브젝트 검색과 씬 저장에서 겹치므로 받지 않는다.
        if (!StaticFindObjectFast(nullptr, Actor->GetOuter(), NewName))
            Actor->SetName(NewName);
        else
            HTR_LOG(Warning, "An actor with the name '{}' already exists in this level.", ActorRenameBuffer);
    }
    RenamingActor = nullptr;
}

// Actor를 ImGui TreeNode로 출력
void FOutlinerPanel::DrawActorNode(const FOutlinerRow& Row)
{
    AActor* Actor = Row.Actor;
    if (!Actor)
        return;

    const FString& ActorName = Actor->GetName();

    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_OpenOnArrow;
    if (!Row.bHasChildren)
        Flags |= ImGuiTreeNodeFlags_Leaf;

    // Actor 선택 시 UI 선택 상태
    if (Selection ? Selection->IsSelected(Actor) : SelectedObject == Actor)
        Flags |= ImGuiTreeNodeFlags_Selected;

    // 깊이만큼 들여 쓴다. TreePush를 쓰지 않으므로 줄마다 직접 Indent·Unindent한다.
    const float IndentWidth = Row.Depth * ImGui::GetStyle().IndentSpacing;
    if (IndentWidth > 0.0f)
        ImGui::Indent(IndentWidth);
    struct FUnindent { float Width; ~FUnindent() { if (Width > 0.0f) ImGui::Unindent(Width); } } Unindent{ IndentWidth };

    const bool bCollapsed = CollapsedActors.contains(Actor);
    if (Row.bHasChildren)
        ImGui::SetNextItemOpen(!bCollapsed, ImGuiCond_Always);

    const bool bIsRenaming = RenamingActor == Actor;
    const bool bOpen = ImGui::TreeNodeEx(Actor, Flags, "%s", bIsRenaming ? "" : ActorName.c_str());
    if (Row.bHasChildren && bOpen == bCollapsed)
    {
        if (bOpen) CollapsedActors.erase(Actor);
        else CollapsedActors.insert(Actor);
    }

    if (bIsRenaming)
    {
        DrawActorRenameField(Actor);
        return;
    }

    // UI에서 Actor 클릭. Shift·Ctrl을 누르고 있으면 기존 선택에 더하거나 뺀다.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        const ImGuiIO& IO = ImGui::GetIO();
        if (Selection && (IO.KeyShift || IO.KeyCtrl))
            ToggleActorSelection(Actor);
        else
            SelectActor(Actor);
    }

    // 더블 클릭으로 이름 편집을 시작한다. 계층 화살표를 빠르게 두 번 누른 것은 제외한다.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        BeginActorRename(Actor);

    // 다른 액터 위에 끌어다 놓으면 그 액터에 붙인다 (TEAM5).
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(EditorDragDrop::Actor, &Actor, sizeof(AActor*));
        ImGui::TextUnformatted(ActorName.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
        {
            PendingAttachActor = *static_cast<AActor**>(Payload->Data);
            PendingAttachParent = Actor;
            bPendingAttach = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (SelectedObject == Actor && !ImGui::GetIO().WantTextInput
        && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        BeginActorRename(Actor);
    }

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Rename", "F2"))
            BeginActorRename(Actor);

        if (ImGui::MenuItem("Focus", "F") && DoubleClickCallback)
            DoubleClickCallback(Actor);

        if (ImGui::MenuItem("Detach", nullptr, false, Actor->GetAttachParentActor() != nullptr))
        {
            PendingAttachActor = Actor;
            PendingAttachParent = nullptr;
            bPendingAttach = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del"))
            PendingDeleteActor = Actor; // 여기서는 바로 삭제 X

        ImGui::EndPopup();
    }
}

// 선택 Object 변경
void FOutlinerPanel::SelectActor(AActor* Actor)
{
    if (Selection)
        Selection->Select(Actor);
    SelectedObject = Actor;
    if (Actor)
        HTR_LOG(Info, "{} UUID {} is selected", Actor->GetName(), Actor->GetUUID());
    NotifyPrimaryChanged();
}

void FOutlinerPanel::ToggleActorSelection(AActor* Actor)
{
    if (!Selection)
    {
        SelectActor(Actor);
        return;
    }
    Selection->Toggle(Actor);
    SelectedObject = Selection->GetPrimary();
    NotifyPrimaryChanged();
}

void FOutlinerPanel::RefreshFromSelection()
{
    SelectedObject = Selection ? Selection->GetPrimary() : nullptr;
    NotifyPrimaryChanged();
}

void FOutlinerPanel::NotifyPrimaryChanged()
{
    AActor* Primary = Cast<AActor>(SelectedObject);
    USceneComponent* Scene = Primary ? Cast<USceneComponent>(Primary->GetRootComponent()) : nullptr;
    if (Callback)
        Callback(Scene);
}


AActor* FOutlinerPanel::GetSelectedActor() const
{
    return Cast<AActor>(SelectedObject);
}
