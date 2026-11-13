#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <functional>
#include <unordered_set>

class UWorld;
class ULevel;
class AActor;
class UActorComponent;
class UPrimitiveComponent;
class USceneComponent;
class FEditorSelection;

using SelectionCallback = std::function<void(USceneComponent*)>;
using DeleteActorCallback = std::function<void(AActor*)>;
using ActorDoubleClickCallback = std::function<void(AActor*)>;

// 레벨의 액터 목록. 더블 클릭·F2·우클릭 메뉴로 액터 이름을 변경한다.
class FOutlinerPanel : public IEditorPanel
{
public:
    FOutlinerPanel() = default;
    ~FOutlinerPanel() = default;

    void SetSelectionCallback(SelectionCallback InCallback) { Callback = InCallback; }
    void SetDeleteActorCallback(DeleteActorCallback InCallback) { DeleteCallback = InCallback;}
    // Focus 메뉴에서 액터로 뷰포트 카메라를 옮길 때 쓴다.
    void SetActorDoubleClickCallback(ActorDoubleClickCallback InCallback) { DoubleClickCallback = InCallback; }
    // 에디터 전체가 공유하는 다중 선택 목록 (TEAM1). 연결하지 않으면 한 개만 선택한다.
    void SetSelection(FEditorSelection* InSelection) { Selection = InSelection; }

public:
    bool Init() override;
    void Tick(float DeltaTime) override;
    void OnRender() override;
    const char* GetPanelName() const override { return "Outliner"; }

    void SetWorld(UWorld* InWorld) { World = InWorld; RenamingActor = nullptr; };

    void DrawActors(ULevel* Level);
    struct FOutlinerRow
    {
        AActor* Actor;
        int32 Depth;
        bool bHasChildren;
    };
    void DrawActorNode(const FOutlinerRow& Row);
    // 기존 선택을 지우고 Actor 하나만 선택한다. nullptr이면 선택을 비운다.
    void SelectActor(AActor* Actor);
    // Actor를 선택 목록에 넣거나 뺀다 (Shift·Ctrl 클릭). 마지막에 넣은 액터가 기즈모·디테일 기준이다.
    void ToggleActorSelection(AActor* Actor);
    // 선택 목록을 밖에서 바꾼 뒤(붙여넣기, 삭제된 액터 정리) Primary와 기즈모·디테일을 다시 맞춘다.
    void RefreshFromSelection();

    AActor* GetSelectedActor() const;

private:
    void BeginActorRename(AActor* Actor);
    // 이름을 바꾸는 줄의 입력칸. Enter·포커스 이탈이면 확정, Esc면 취소한다.
    void DrawActorRenameField(AActor* Actor);

    UWorld* World = nullptr;
    UObject* SelectedObject = nullptr;
    FEditorSelection* Selection = nullptr;

    // Primary 액터의 루트를 콜백으로 알린다 (기즈모·디테일·외곽선 갱신)
    void NotifyPrimaryChanged();

    SelectionCallback Callback;
    DeleteActorCallback DeleteCallback;
    ActorDoubleClickCallback DoubleClickCallback;
    AActor* PendingDeleteActor = nullptr;

    // 부모 액터 아래에 자식을 펼쳐 놓은 줄 목록. 매 프레임 다시 만든다.
    void AddActorRows(AActor* Actor, int32 Depth, ULevel* Level);
    TArray<FOutlinerRow> Rows;
    std::unordered_set<AActor*> CollapsedActors;
    // 목록을 그리는 중에 계층을 바꾸지 않도록 끝난 뒤 적용한다.
    AActor* PendingAttachActor = nullptr;
    AActor* PendingAttachParent = nullptr;
    bool bPendingAttach = false;

    AActor* RenamingActor = nullptr;
    char ActorRenameBuffer[256] = {};
    bool bFocusActorRename = false;
};
