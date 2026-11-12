#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Animation/AnimSequence.h"

class FAnimSequenceEditor : public FSkeletalPreviewEditor
{
public:
    FAnimSequenceEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
        const FString& InAssetPath, UAnimSequence* InSequence);

    const char* GetEditorTypeName() const override { return "AnimSequence"; }

    // 재생 API — 패널은 이것만 부른다
    void  Play();
    void  Pause();
    void  SetPlaybackTime(float Time);   // 스크럽: Pause + 시간 설정 + bPoseDirty
    void  StepFrame(int32 Delta);        // ±1프레임 (Pause)
    
    bool  IsPlaying() const { return bPlaying; }
    void  MarkPoseDirty() { bPoseDirty = true; }  // 설정이 바뀌어 같은 시간의 포즈를 다시 계산해야 할 때
    float GetPlaybackTime() const { return CurrentTime; }
    float GetPlayLength() const;
    float GetFrameRate() const;
    int32 GetNumFrames() const;
    UAnimSequence* GetSequence() const { return Sequence; }

    // Asset Browser에서 더블클릭한 애니메이션을 연다 (Unreal처럼 새 탭 없이 이 뷰어에서 바꾼다).
    // 다른 탭에 이미 열려 있으면 그 탭으로 간다. 저장하지 않은 변경이 있거나 미리보기 메시와 맞지 않으면 바꾸지 않는다.
    void OpenFromAssetBrowser(const FString& InAssetPath);

    // Notify 선택 (타임라인과 디테일 패널이 공유). 배열 Index는 정렬마다 바뀌므로 Guid로 가리킨다.
    const FGuid& GetSelectedNotify() const { return SelectedNotify; }
    void SelectNotify(const FGuid& Guid) { SelectedNotify = Guid; }
    void ClearNotifySelection() { SelectedNotify.Invalidate(); }
    // 선택한 Notify가 지워졌으면 nullptr. 편집 함수를 부른 뒤에는 포인터가 바뀔 수 있으니 다시 받는다.
    const FAnimNotifyEvent* GetSelectedNotifyEvent() const
    {
        return (Sequence && SelectedNotify.IsValid()) ? Sequence->FindNotify(SelectedNotify) : nullptr;
    }

    // 저장하지 않은 Notify/트랙 편집이 있는지.
    // 편집한 곳은 MarkNotifiesDirty를 부르고, 마우스를 놓은 뒤 CommitNotifyUndo가 실행 취소 단계 하나로 묶는다.
    void MarkNotifiesDirty() { bNotifiesDirty = true; bUndoCommitPending = true; }
    bool HasUnsavedNotifyChanges() const { return bNotifiesDirty; }
    // Notify 또는 Root Motion 설정(Enable / Root Lock / Force)이 저장된 값과 다르면 dirty (Unreal은 에셋 프로퍼티를 바꾸면 dirty)
    bool IsDirty() const override { return bNotifiesDirty || HasUnsavedRootMotionChanges(); }
    bool HasUnsavedRootMotionChanges() const;

    // .anim.bin(v3)에 덮어쓴다. 임시 파일에 먼저 써서 실패해도 원본이 깨지지 않는다.
    bool Save();
    // 파일에서 다시 읽어 저장하지 않은 편집을 버린다. 이 시퀀스는 AssetManager 캐시를 공유하므로
    // 버리지 않고 닫으면 편집 내용이 메모리에 남아 런타임에도 보인다.
    bool RevertToSaved();
    const FString& GetSaveMessage() const { return SaveMessage; }

    // 저장 안 한 변경이 있으면 닫기를 미루고 확인 창을 띄운다
    bool ConfirmClose() override;
    bool IsUnsavedPromptRequested() const { return bUnsavedPromptRequested; }
    void ClearUnsavedPromptRequest() { bUnsavedPromptRequested = false; }
    // 확인 창의 선택 결과로 닫는다 (다시 묻지 않는다)
    void CloseWithoutPrompt();

    // ---- 실행 취소 (Notify/트랙 편집) ----
    // 매 프레임 끝에 부른다. 조작 중(드래그, 입력 중)이 아니고 편집이 있었으면 이전 상태를 Undo 스택에 넣는다.
    void CommitNotifyUndo(bool bInteractionActive);
    void UndoNotifyEdit();
    void RedoNotifyEdit();
    bool CanUndoNotifyEdit() const { return !UndoStack.IsEmpty(); }
    bool CanRedoNotifyEdit() const { return !RedoStack.IsEmpty(); }

    bool  bLooping = true;
    float PlayRate = 1.0f;
    bool  bPreviewNotifies = true;   // 재생 중 Notify를 실제로 발동 (사운드 등)

    // 방금 발동한 Notify면 1 → 0으로 줄어드는 값 (타임라인 반짝임용)
    float GetNotifyFlash(const FGuid& Guid) const;

protected:
    TUniquePtr<IEditorTab> CreateTab() override;
    bool InitializeResources() override;
    void ShutdownResources() override;
    void TickBeforeWorld(float DeltaTime) override;

private:
    bool IsCompatible(const USkeletalMesh& InMesh) const;
    int32 FindRootBoneIndex() const;

    UAnimSequence* Sequence = nullptr;
    FCompactPose PlaybackPose;
    TArray<int32> FiredNotifies;
    float CurrentTime = 0.0f;
    bool  bPlaying = true;            
    bool  bPoseDirty = true;

    // 루트 모션 미리보기 (Unreal 뷰어 Process Root Motion = LoopAndReset)
    // root는 포즈에서 고정되고, 그만큼 미리보기 메시 컴포넌트를 옮긴다. 루프하면 처음 위치로 돌아간다
    void ApplyPreviewRootMotion();
    FTransform PreviewBaseTransform;   // 루트 모션을 적용하기 전 메시 컴포넌트 위치
    bool  bHasPreviewBase = false;

    FGuid SelectedNotify;
    bool  bNotifiesDirty = false;

    // 마지막으로 저장(또는 열기·되돌리기)했을 때의 Root Motion 설정. 지금 값과 비교해 dirty를 판단한다
    struct FRootMotionSettings
    {
        bool bEnableRootMotion = false;
        bool bForceRootLock = false;
        uint8 RootLock = 0;
        bool operator==(const FRootMotionSettings& Other) const = default;
    };
    FRootMotionSettings CaptureRootMotionSettings() const;
    FRootMotionSettings SavedRootMotionSettings;

    FString SaveMessage;
    bool  bUnsavedPromptRequested = false;
    bool  bCloseConfirmed = false;

    // ---- 미리보기 Notify ----
    // 진행 중인 NotifyState는 포인터 대신 Guid로 기억한다. 재생 중에 Notify를 편집/삭제해도
    // 배열 원소 주소가 바뀌는 문제를 피한다 (지워진 State는 End 없이 빠진다).
    void TriggerPreviewNotifies(float PrevTime, float DeltaMove, float FrameDeltaTime);
    void EndPreviewNotifyStates();
    void FlashNotify(const FGuid& Guid);

    // ---- 실행 취소 ----
    void ResetNotifyHistory();          // 스택을 비우고 지금 상태를 저장된 상태로 본다
    void ApplyNotifySnapshot(const TArray<uint8>& Snapshot);
    TArray<TArray<uint8>> UndoStack;
    TArray<TArray<uint8>> RedoStack;
    TArray<uint8> CommittedSnapshot;    // 마지막으로 확정된 상태
    TArray<uint8> SavedSnapshot;        // 파일에 있는 상태 (같으면 변경 없음)
    bool bUndoCommitPending = false;
    static constexpr int32 MaxUndoSteps = 100;

    TArray<FGuid> ActivePreviewStates;
    struct FNotifyFlash { FGuid Guid; float Time; };
    TArray<FNotifyFlash> RecentFired;
    float EditorTime = 0.0f;
};