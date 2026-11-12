#include "EnginePCH.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditorTab.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/AnimNotify.h"
#include "Animation/AnimNotifyState.h"
#include "Animation/AnimNotifyQueue.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "UObject/Casts.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

FAnimSequenceEditor::FAnimSequenceEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
    const FString& InAssetPath, UAnimSequence* InSequence)
    : FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
    , Sequence(InSequence)
{
}

bool FAnimSequenceEditor::Save()
{
    if (!Sequence)
    {
        return false;
    }

    const FString Resolved = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
    const std::filesystem::path FilePath(Resolved.empty() ? GetAssetPath() : Resolved);
    std::filesystem::path TempPath = FilePath;
    TempPath += ".saving";

    // 임시 파일에 쓰고 바꿔치기한다. 쓰다가 실패해도 원본 .anim.bin은 그대로 남는다.
    FString Error;
    if (!AnimSequenceFile::Write(TempPath, *Sequence, Error))
    {
        std::error_code Ignored;
        std::filesystem::remove(TempPath, Ignored);
        SaveMessage = "Save failed: " + Error;
        HTR_LOG(Error, "AnimSequence save failed: {} ({})", GetAssetPath(), Error);
        return false;
    }

    std::error_code Ec;
    std::filesystem::rename(TempPath, FilePath, Ec);   // 같은 볼륨이면 기존 파일을 교체한다
    if (Ec)
    {
        std::error_code Ignored;
        std::filesystem::remove(TempPath, Ignored);
        SaveMessage = "Save failed: cannot replace file (" + Ec.message() + ")";
        HTR_LOG(Error, "AnimSequence save failed: {} ({})", GetAssetPath(), Ec.message());
        return false;
    }

    bNotifiesDirty = false;
    Sequence->CaptureNotifySnapshot(SavedSnapshot);   // 실행 취소로 이 상태에 돌아오면 변경 없음
    SavedRootMotionSettings = CaptureRootMotionSettings();
    SaveMessage = "Saved.";
    HTR_LOG(Info, "AnimSequence saved: {}", GetAssetPath());
    return true;
}

bool FAnimSequenceEditor::RevertToSaved()
{
    if (!Sequence)
    {
        return false;
    }

    // 다시 읽으면 Notify 객체가 새로 만들어지므로 진행 중인 State를 먼저 닫는다
    EndPreviewNotifyStates();

    const FString Resolved = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
    FString Error;
    if (!AnimSequenceFile::Read(std::filesystem::path(Resolved.empty() ? GetAssetPath() : Resolved), *Sequence, Error))
    {
        SaveMessage = "Revert failed: " + Error;
        HTR_LOG(Error, "AnimSequence revert failed: {} ({})", GetAssetPath(), Error);
        return false;
    }

    ClearNotifySelection();
    ResetNotifyHistory();
    bPoseDirty = true;
    SaveMessage = "Reverted to saved file.";
    return true;
}

void FAnimSequenceEditor::TriggerPreviewNotifies(float PrevTime, float DeltaMove, float FrameDeltaTime)
{
    if (DeltaMove == 0.0f)
    {
        return;
    }

    FAnimNotifyContext Context(bLooping, Sequence);
    Sequence->GetAnimNotifies(PrevTime, DeltaMove, Context);

    // UAnimInstance::TriggerAnimNotifies와 같은 순서: End → 단발/Begin → Tick
    TArray<FGuid> NewActive;
    for (const FAnimNotifyEventReference& Ref : Context.ActiveNotifies)
    {
        const FAnimNotifyEvent* Event = Ref.GetNotify();
        if (Event && Event->NotifyStateClass && NewActive.Find(Event->Guid) == -1)
            NewActive.Add(Event->Guid);
    }

    for (const FGuid& Guid : ActivePreviewStates)
    {
        if (NewActive.Find(Guid) != -1)
            continue;
        if (const FAnimNotifyEvent* Event = Sequence->FindNotify(Guid); Event && Event->NotifyStateClass)
            Event->NotifyStateClass->NotifyEnd(MeshComponent, Sequence, FAnimNotifyEventReference(Event, Sequence));
    }

    for (const FAnimNotifyEventReference& Ref : Context.ActiveNotifies)
    {
        const FAnimNotifyEvent* Event = Ref.GetNotify();
        if (!Event)
            continue;

        if (Event->NotifyStateClass)
        {
            if (ActivePreviewStates.Find(Event->Guid) == -1)
            {
                Event->NotifyStateClass->NotifyBegin(MeshComponent, Sequence, Event->Duration, Ref);
                FlashNotify(Event->Guid);
            }
        }
        else if (Event->Notify)
        {
            Event->Notify->Notify(MeshComponent, Sequence, Ref);
            FlashNotify(Event->Guid);
        }
        else
        {
            // 이름 Notify는 받을 AnimInstance가 없으므로 로그와 반짝임으로만 보여 준다
            HTR_LOG(Info, "[AnimPreview] Notify: {}", Event->NotifyName.ToString());
            FlashNotify(Event->Guid);
        }
    }

    ActivePreviewStates = std::move(NewActive);
    for (const FGuid& Guid : ActivePreviewStates)
    {
        if (const FAnimNotifyEvent* Event = Sequence->FindNotify(Guid); Event && Event->NotifyStateClass)
            Event->NotifyStateClass->NotifyTick(MeshComponent, Sequence, FrameDeltaTime, FAnimNotifyEventReference(Event, Sequence));
    }
}

void FAnimSequenceEditor::EndPreviewNotifyStates()
{
    for (const FGuid& Guid : ActivePreviewStates)
    {
        if (!Sequence || !MeshComponent)
            break;
        if (const FAnimNotifyEvent* Event = Sequence->FindNotify(Guid); Event && Event->NotifyStateClass)
            Event->NotifyStateClass->NotifyEnd(MeshComponent, Sequence, FAnimNotifyEventReference(Event, Sequence));
    }
    ActivePreviewStates.Empty();
}

void FAnimSequenceEditor::FlashNotify(const FGuid& Guid)
{
    for (FNotifyFlash& Flash : RecentFired)
    {
        if (Flash.Guid == Guid)
        {
            Flash.Time = EditorTime;
            return;
        }
    }
    RecentFired.Add({ Guid, EditorTime });
}

float FAnimSequenceEditor::GetNotifyFlash(const FGuid& Guid) const
{
    constexpr float FlashSeconds = 0.35f;
    for (const FNotifyFlash& Flash : RecentFired)
    {
        if (Flash.Guid == Guid)
            return std::clamp(1.0f - (EditorTime - Flash.Time) / FlashSeconds, 0.0f, 1.0f);
    }
    return 0.0f;
}

bool FAnimSequenceEditor::ConfirmClose()
{
    if (!IsDirty() || bCloseConfirmed)
    {
        return true;
    }

    // 탭이 다음 프레임에 확인 창을 연다
    bUnsavedPromptRequested = true;
    return false;
}

void FAnimSequenceEditor::CloseWithoutPrompt()
{
    bCloseConfirmed = true;
    Close();
}

bool FAnimSequenceEditor::InitializeResources()
{
    if (!Sequence || !Sequence->GetDataModel())
    {
        HTR_LOG(Warning, "AnimSequence has no animation data: {}", GetAssetPath());
        return false;
    }

    const FString& PreviewPath = Sequence->GetPreviewMeshPath();
    if (PreviewPath.empty())
    {
        HTR_LOG(Warning, "AnimSequence has no preview mesh. Re-import its FBX to record one: {}", GetAssetPath());
        return false;
    }

    // 경로 표기가 달라도 같은 파일을 찾도록 실제 파일 경로로 바꾼 뒤 로드한다 (EditorManager와 같은 방식)
    const FString FilePath = FAssetUtils::ResolveAssetFilePath(PreviewPath);
    FString Error;
    USkeletalMesh* PreviewMesh = Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(FilePath.empty() ? PreviewPath : FilePath, Error));
    if (!PreviewMesh)
    {
        HTR_LOG(Warning, "AnimSequence preview mesh failed to load: {} ({})", PreviewPath, Error);
        return false;
    }

    if (!IsCompatible(*PreviewMesh))
    {
        HTR_LOG(Warning, "AnimSequence tracks do not match the preview mesh bones: {}", GetAssetPath());
        return false;
    }

    Mesh = PreviewMesh;
    if (!InitializePreview())
    {
        return false;
    }

    CurrentTime = 0.0f;
    bPoseDirty = true;
    ResetNotifyHistory();
    return true;
}

// ---------------- 실행 취소 ----------------

void FAnimSequenceEditor::ResetNotifyHistory()
{
    UndoStack.Empty();
    RedoStack.Empty();
    if (Sequence) Sequence->CaptureNotifySnapshot(CommittedSnapshot);
    SavedSnapshot = CommittedSnapshot;
    SavedRootMotionSettings = CaptureRootMotionSettings();   // 열기·되돌리기 직후 값이 저장된 값
    bUndoCommitPending = false;
    bNotifiesDirty = false;
}

FAnimSequenceEditor::FRootMotionSettings FAnimSequenceEditor::CaptureRootMotionSettings() const
{
    FRootMotionSettings Settings;
    if (Sequence)
    {
        Settings.bEnableRootMotion = Sequence->GetEnableRootMotion();
        Settings.bForceRootLock = Sequence->GetForceRootLock();
        Settings.RootLock = static_cast<uint8>(Sequence->GetRootMotionRootLock());
    }
    return Settings;
}

bool FAnimSequenceEditor::HasUnsavedRootMotionChanges() const
{
    return !(CaptureRootMotionSettings() == SavedRootMotionSettings);
}

void FAnimSequenceEditor::CommitNotifyUndo(bool bInteractionActive)
{
    // 드래그나 글자 입력 중에는 미룬다. 놓는 순간 한 단계로 확정된다.
    if (!bUndoCommitPending || bInteractionActive || !Sequence)
    {
        return;
    }
    bUndoCommitPending = false;

    TArray<uint8> Current;
    if (!Sequence->CaptureNotifySnapshot(Current) || Current == CommittedSnapshot)
    {
        bNotifiesDirty = !(Current == SavedSnapshot);
        return;
    }

    UndoStack.Add(std::move(CommittedSnapshot));
    if (UndoStack.Num() > MaxUndoSteps)
        UndoStack.RemoveAt(0);
    RedoStack.Empty();
    CommittedSnapshot = std::move(Current);

    // 편집하다 저장된 상태로 돌아오면 변경 없음으로 본다
    bNotifiesDirty = !(CommittedSnapshot == SavedSnapshot);
}

void FAnimSequenceEditor::ApplyNotifySnapshot(const TArray<uint8>& Snapshot)
{
    // 복원하면 Notify 객체가 새로 만들어지므로 진행 중인 State를 먼저 닫는다
    EndPreviewNotifyStates();
    if (!Sequence->RestoreNotifySnapshot(Snapshot))
    {
        HTR_LOG(Error, "AnimSequence undo snapshot restore failed: {}", GetAssetPath());
        return;
    }
    // Guid가 유지되므로 선택은 그대로 둔다 (그 Notify가 없는 상태면 선택이 자연히 비어 보인다)
    bNotifiesDirty = !(Snapshot == SavedSnapshot);
}

void FAnimSequenceEditor::UndoNotifyEdit()
{
    CommitNotifyUndo(false);   // 확정 안 된 편집이 있으면 먼저 한 단계로 만든다
    if (UndoStack.IsEmpty() || !Sequence)
    {
        return;
    }

    RedoStack.Add(std::move(CommittedSnapshot));
    CommittedSnapshot = std::move(UndoStack[UndoStack.Num() - 1]);
    UndoStack.RemoveAt(UndoStack.Num() - 1);
    ApplyNotifySnapshot(CommittedSnapshot);
}

void FAnimSequenceEditor::RedoNotifyEdit()
{
    CommitNotifyUndo(false);
    if (RedoStack.IsEmpty() || !Sequence)
    {
        return;
    }

    UndoStack.Add(std::move(CommittedSnapshot));
    CommittedSnapshot = std::move(RedoStack[RedoStack.Num() - 1]);
    RedoStack.RemoveAt(RedoStack.Num() - 1);
    ApplyNotifySnapshot(CommittedSnapshot);
}

void FAnimSequenceEditor::ShutdownResources()
{
    bPlaying = false;
    EndPreviewNotifyStates();   // 메시가 남아 있을 때 닫는다
    FSkeletalPreviewEditor::ShutdownResources();
}

void FAnimSequenceEditor::OpenFromAssetBrowser(const FString& InAssetPath)
{
    if (InAssetPath == GetAssetPath() || !Mesh)
    {
        return;
    }

    if (FAssetEditor* Other = EditorManager.FindEditor(InAssetPath))
    {
        Other->Activate();
        return;
    }

    if (IsDirty())
    {
        SaveMessage = "Save or revert before opening another animation.";
        return;
    }

    UAnimSequence* NewSequence = UAssetManager::LoadAnimSequence(InAssetPath);
    if (!NewSequence || !NewSequence->GetDataModel())
    {
        SaveMessage = "Cannot load " + InAssetPath;
        return;
    }

    // 진행 중인 State는 지금 시퀀스 기준으로 닫고 바꾼다. 미리보기 메시는 그대로 쓰므로 트랙이 맞아야 한다.
    EndPreviewNotifyStates();
    UAnimSequence* Previous = Sequence;
    Sequence = NewSequence;
    if (!IsCompatible(*Mesh))
    {
        Sequence = Previous;
        SaveMessage = "Tracks do not match the preview mesh: " + InAssetPath;
        return;
    }

    SetAssetPath(InAssetPath);
    CurrentTime = 0.0f;
    bPlaying = true;
    bPoseDirty = true;
    ClearNotifySelection();
    RecentFired.Empty();
    ResetNotifyHistory();
    SaveMessage.clear();
}

TUniquePtr<IEditorTab> FAnimSequenceEditor::CreateTab()
{
    return MakeUnique<FAnimSequenceEditorTab>(*this);
}

float FAnimSequenceEditor::GetPlayLength() const
{
    const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
    return Model ? Model->GetPlayLength() : 0.0f;
}

float FAnimSequenceEditor::GetFrameRate() const
{
    const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
    const float Rate = Model ? static_cast<float>(Model->GetFrameRate().AsDecimal()) : 0.0f;
    return Rate > 0.0f ? Rate : 30.0f;
}

int32 FAnimSequenceEditor::GetNumFrames() const
{
    const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
    return Model ? Model->GetNumberOfFrames() : 0;
}

void FAnimSequenceEditor::Play()
{
    // 루프가 아니고 끝에 멈춰 있으면 처음부터 다시 재생한다
    if (!bLooping && CurrentTime >= GetPlayLength())
    {
        CurrentTime = 0.0f;
        bPoseDirty = true;
    }
    bPlaying = true;
}

void FAnimSequenceEditor::Pause()
{
    bPlaying = false;
}

void FAnimSequenceEditor::SetPlaybackTime(float Time)
{
    bPlaying = false;
    CurrentTime = std::clamp(Time, 0.0f, GetPlayLength());
    bPoseDirty = true;
}

void FAnimSequenceEditor::StepFrame(int32 Delta)
{
    const float Rate = GetFrameRate();
    const int32 Frame = static_cast<int32>(std::lround(CurrentTime * Rate)) + Delta;
    SetPlaybackTime(static_cast<float>(Frame) / Rate);
}

bool FAnimSequenceEditor::IsCompatible(const USkeletalMesh& InMesh) const
{
    // GetAnimationPose는 트랙을 본 이름으로 찾는다. 순서가 아니라 이름이 모두 있는지를 본다
    const TArray<FBoneAnimationTrack>& Tracks = Sequence->GetDataModel()->GetBoneAnimationTracks();
    if (Tracks.IsEmpty())
    {
        return false;
    }

    for (const FBoneAnimationTrack& Track : Tracks)
    {
        if (InMesh.FindBoneIndex(Track.Name) < 0)
        {
            return false;
        }
    }
    return true;
}

void FAnimSequenceEditor::TickBeforeWorld(float DeltaTime)
{
    if (!Sequence || !MeshComponent || !Mesh)
    {
        return;
    }

    EditorTime += DeltaTime;

    const float Length = GetPlayLength();
    if (bPlaying && Length > 0.0f)
    {
        const float PrevTime = CurrentTime;
        float Next = CurrentTime + DeltaTime * PlayRate;
        if (bLooping)
        {
            // fmod는 음수에 음수를 돌려주므로 Length를 더해 한 번 더 나눈다 (역재생 대응)
            Next = std::fmod(std::fmod(Next, Length) + Length, Length);
        }
        else if (Next >= Length || Next < 0.0f)
        {
            Next = std::clamp(Next, 0.0f, Length);
            bPlaying = false;
        }

        CurrentTime = Next;
        bPoseDirty = true;

        if (bPreviewNotifies)
        {
            // 루프가 아니면 끝에서 멈춘 만큼만 진행한 것으로 본다
            const float DeltaMove = bLooping ? DeltaTime * PlayRate : (CurrentTime - PrevTime);
            TriggerPreviewNotifies(PrevTime, DeltaMove, DeltaTime);
        }
    }

    // 멈추거나(일시정지/스크럽/끝) 미리보기를 끄면 진행 중인 State를 닫는다
    if ((!bPlaying || !bPreviewNotifies) && !ActivePreviewStates.IsEmpty())
    {
        EndPreviewNotifyStates();
    }

    if (bPoseDirty)
    {
        // 트랙이 없는 본은 GetAnimationPose 안에서 바인드 포즈로 남는다
        Sequence->GetAnimationPose(CurrentTime, PlaybackPose, Mesh->GetRefSkeleton());
        MeshComponent->SetBoneSpaceTransforms(PlaybackPose);
        bPoseDirty = false;
    }

    ApplyPreviewRootMotion();
}

void FAnimSequenceEditor::ApplyPreviewRootMotion()
{
    if (!bHasPreviewBase)
    {
        PreviewBaseTransform = MeshComponent->GetTransform();
        bHasPreviewBase = true;
    }

    // 1. 기준 위치로 되돌린다 (루트 모션이 꺼져 있으면 여기서 끝 → 제자리)
    MeshComponent->SetTransform(PreviewBaseTransform);
    if (!Sequence->GetEnableRootMotion() || CurrentTime <= 0.0f)
    {
        return;
    }

    // 2. 0초 → 현재 시간 동안 root가 움직인 양 (root 부모 공간). 루프하면 CurrentTime이 0 근처로 돌아가 자연히 처음 위치가 된다
    const FTransform LocalDelta = Sequence->ExtractRootMotion(0.0f, CurrentTime, false);

    // 3. 월드 공간으로 바꿔 메시 컴포넌트를 옮긴다 (게임에서 CharacterMovement가 캡슐을 옮기는 것과 같은 값)
    const FTransform WorldDelta = MeshComponent->ConvertRootMotionToWorld(LocalDelta);
    FTransform Moved = PreviewBaseTransform;
    Moved.Location = PreviewBaseTransform.Location + WorldDelta.Location;
    Moved.Rotation = (WorldDelta.Rotation * PreviewBaseTransform.Rotation).Normalized();
    MeshComponent->SetTransform(Moved);
}

int32 FAnimSequenceEditor::FindRootBoneIndex() const
{
    if (!Mesh)
    {
        return -1;
    }

    // UAnimSequence::FindRootTrack과 같은 규칙: "root"를 먼저, 없으면 첫 트랙의 본
    const int32 RootIndex = Mesh->FindBoneIndex("root");
    if (RootIndex >= 0)
    {
        return RootIndex;
    }

    const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
    if (!Model || Model->GetBoneAnimationTracks().IsEmpty())
    {
        return -1;
    }
    return Mesh->FindBoneIndex(Model->GetBoneAnimationTracks()[0].Name);
}
