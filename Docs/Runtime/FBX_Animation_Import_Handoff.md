# FBX Animation Import — Unreal식 리팩터링 인수인계

> 대상 파일: `Source/Editor/Asset/FbxImporter/FbxImporter.{h,cpp}`
> 참고 소스: `C:\Users\JUNGLE\Desktop\JungleGameTech\UnrealEngine`
> 작성일: 2026-11-09

---

## 1. 목표

기존 `ReadAnimationData()` 기반 애니메이션 임포트를 **Unreal 구조(ImportAnimations → ImportAnimation → ImportBoneTracks)** 로 단계별 교체한다.
단, **오버엔지니어링 금지** — Unreal의 에디터/패키지/리임포트 전용 기능은 가져오지 않고, W11 엔진에 필요한 부분만 옮긴다.

---

## 2. Unreal 참고 위치

| 기능 | 파일 | 위치 |
|---|---|---|
| `ImportAnimations` | `Engine/Source/Editor/UnrealEd/Private/SkeletalMeshEdit.cpp` | 568줄 |
| `GetAnimationTimeSpan` / `ValidateAnimStack` | 같은 파일 | 356줄 이후, 849줄 이후 |
| `ImportAnimation` | 같은 파일 | 1642줄 |
| `ImportBoneTracks` | 같은 파일 | 2048줄 |
| 메시+애니 동시 임포트 진입점 | `Engine/Source/Editor/UnrealEd/Private/Fbx/FbxSkeletalMeshImport.cpp` | 2532줄 (`AnimName = 메시이름_Anim`) |
| 애니만 임포트 진입점 | `Fbx/FbxFactory.cpp` 874줄 → `SkeletalMeshEdit.cpp` 105줄 | `AnimName = FBX 파일명` |
| 선언 | `Engine/Source/Editor/UnrealEd/Public/FbxImporter.h` | |

---

## 3. 호출 구조 (현재 구현 완료 상태)

```txt
ImportAnimations(OutSequences, OutWarnings, OutError, Options)
├─ 사전 검사: 스켈레톤 읽음 여부, 리샘플 옵션
├─ ResampleRate 결정: 옵션 / GetMaxSampleRate   (기존 코드 유지)
└─ AnimStack마다
   ├─ TimeSpan = GetAnimationTimeSpan(Stack)          // GetLocalTimeSpan (Unreal ExportedTime)
   ├─ ValidateAnimStack(Stack, SampleRate, TimeSpan)  // ★ 프레임 경계 snap, 수정되면 경고
   ├─ Sequence = NewObject<UAnimSequence>(); Initialize(이름, {}, SampleRate, 0)
   ├─ ImportAnimation(Sequence, Stack, SampleRate, TimeSpan, OutWarnings)
   └─ 트랙 0개면 경고 후 skip
→ 유효 스택 0개면 실패

ImportAnimation
├─ NumberOfFrames = max(1, round(길이 × fps)),  NumberOfKeys = Frames + 1
├─ PlayLength = Frames / fps                         // ★ 길이는 여기서만 확정
├─ SampleTimes[i] = Start + i 프레임                 // 누적 오차 없이 마지막 키 = Stop
├─ 스택 활성화 → ImportBoneTracks → 이전 스택 복원
└─ Sequence.Initialize(이름, Tracks, SampleRate, PlayLength)

ImportBoneTracks(SampleTimes, OutTracks, OutWarnings)
├─ Nodes[본 인덱스] = FbxNode*  (BoneIndices 이용, 스켈레톤 순서)
├─ 본마다 트랙 준비 (Name = Bones[i].Name, 키 배열 Reserve)
├─ 샘플 시각마다
│  ├─ 모든 본 Globals[i] = EvaluateGlobalTransform(t)   // 부모 값 재사용
│  └─ 본마다 Local = 루트 ? Global : Global[부모]⁻¹ × Global
│     ├─ NaN/0스케일 검사 → 실패 시 트랙 무효 + 경고 (재생 시 바인드 포즈)
│     ├─ 회전: Q → Euler → FRotator(Pitch=Y, Yaw=Z, Roll=X) → FQuat
│     │        ※ ReadSkeletonData 바인드 포즈와 같은 경로. 일부러 맞춘 것이니 바꾸지 말 것
│     └─ PosKeys / RotKeys / ScaleKeys Add
└─ 유효 트랙만 OutTracks (모든 트랙 키 개수 동일)
```

### 시그니처 변경 내역
- `ValidateAnimStack(const FbxAnimStack&, FFrameRate SampleRate, FbxTimeSpan& InOutTimeSpan, FString& OutError) const`
- `ImportAnimation(UAnimSequence&, FbxAnimStack&, FFrameRate, const FbxTimeSpan&, TArray<FString>& OutWarnings)`
- `ImportBoneTracks(const TArray<FbxTime>& SampleTimes, TArray<FBoneAnimationTrack>& OutTracks, TArray<FString>& OutWarnings)`

### 빌드·검증 상태 (2026-11-09)
HitoriEditor Debug x64 빌드 통과. `UAL1_Standard.fbx` 임포트 시 43개 스택 모두 저장, 재읽기 테스트 PASS.
(이전에 Visual Studio가 열린 파일을 옛 버퍼로 덮어쓴 적이 있음 → 외부 수정 시 VS에서 "다시 불러오기")

---

## 4. Unreal에서 뺀 것 (구현 불필요)

| Unreal | 이유 |
|---|---|
| `CanImportClass`, `UFbxAnimSequenceImportData`, MD5 | 리임포트 메타데이터 → W11은 `ImportToAssets`의 `FFbxSourceInfo`가 담당 |
| `CreatePackage` / `LoadObject` / 기존 에셋 재사용 / `AssetRegistry` | 패키지 시스템 없음, 파일 덮어쓰기 방식 |
| `SanitizeObjectName` | `ImportToAssets`에서 이름 처리 |
| `AnimationLengthImportType` 3종 | ExportedTime(`GetLocalTimeSpan`)만 사용 |
| 모프 타깃 / 블렌드셰이프 / 커스텀 어트리뷰트 / 본 메타데이터 | 미지원 |
| Controller 브래킷·트랜잭션, 리임포트 길이 경고, 커브 스케일 | 에디터 기능 |
| `Converter` 좌표 변환, `JointPostConversionMatrix`, 씬 임포트 루트 보정 | Scene이 이미 엔진 좌표계(바인드 포즈 코드와 같은 전제) |
| `SetSkeleton` | `UAnimSequence`에 스켈레톤 참조 없음, 트랙은 이름 매칭 |
| `LastCreatedAnim` 1개 리턴 | `OutSequences` 배열 반환이 더 적합 |

---

## 5. 역할 분리 원칙

- `ImportAnimations` : 스택 순회 + 시퀀스 준비만
- `ImportAnimation`  : 프레임 수/PlayLength/샘플 시각 결정, 스택 활성화
- `ImportBoneTracks` : 키 추출만
- `ImportToAssets`   : **파일 저장 · 경로 · 리임포트 판단** (애니 함수들에 넣지 말 것)

---

## 6. 후속 작업 — ✅ 모두 완료 (2026-11-09)

- 6-1 ✅ `ImportToAssets`가 `ImportAnimations`를 호출. 애니 임포트가 실패해도 메시는 저장하고 경고만 남긴다.
- 6-2 ✅ 스택마다 파일 저장. 파일명은 `SanitizeFileName`으로 정리(`|` → `_`), 겹치면 `_1`, `_2`. 파일 안 `ClipName`은 원본 유지.
  이번에 쓰지 않은 기존 `Name.anim.bin` / `Name_*.anim.bin`은 삭제. 리임포트 스킵은 "애니 파일이 하나 이상 있고 모두 읽힘" 기준.
  (파일 목록을 따로 기록하지 않으므로, 스택 수만 바뀐 경우를 감지하지 못한다 → 폴더 삭제 후 재임포트)
- 6-3 ✅ `ReadAnimationData`, `BuildAnimationSampleTimes`, `ReadBoneAnimationTracks`, `FAnimationClipData` 삭제.

아래는 작업 당시 계획 기록.

### 6-1. `ImportToAssets`를 새 경로로 교체
(당시) `ImportToAssets`는 여전히 `ReadAnimationData(0, 30fps)`를 직접 호출하고 스택 0번만 저장했다.
→ `ImportAnimations(...)` 호출로 교체.

### 6-2. 다중 스택 저장 규칙 (Unreal 방식)
Unreal: `ImportAnimations` 622줄
```cpp
SequenceName = Name;
if (ValidTakeCount > 1) SequenceName += "_" + AnimStackName;   // 유효 스택 2개 이상일 때만
```
스택마다 별도 파일. 예) `Hero.fbx`에 Walk/Run/Jump → `Hero_Anim_Walk.uasset`, `Hero_Anim_Run.uasset`, ...

W11 적용안:
- 스택 1개: `Assets/FBX/Name/Name.anim.bin` (현행 유지)
- 스택 여러 개: `Name_Walk.anim.bin`, `Name_Run.anim.bin`, ...

함께 고칠 곳:
- `FFbxImportResult::AnimationPath`(단일) → `AnimationPaths`(배열)
- 리임포트 스킵 판단(`AnimationChecked`, `exists(AnimationPath)`)을 여러 파일 기준으로
- 스택 이름의 파일명 불가 문자 정리 (Sanitize)
- `ImportAnimations`에서 `ValidTakeCount`(유효 스택 수)에 따라 시퀀스 이름 결정

### 6-3. 정리
교체 후 `ReadAnimationData`, `ReadBoneAnimationTracks` 삭제.

---

## 7. 배경 지식 요약

- **FBX 원본 구조**: AnimStack(클립) → AnimLayer → CurveNode(노드.속성, 예: `Arm.LclRotation`) → X/Y/Z 채널별 `FbxAnimCurve` → Key[]
  - 뼈 단위가 아니라 **속성·축 단위로 흩어짐**, 축마다 Key 시각이 다름, 회전은 오일러(°)
- **Key 간격**: 고정 아님. 애니메이터가 찍은 위치 / Bake 시 씬 fps / Key Reduction에 따라 불규칙.
  - 원본 fps: `Scene->GetGlobalSettings().GetTimeMode()` → `FbxTime::GetFrameRate()`
  - Key 시각: `Curve->KeyGetTime(i)`
- **우리는 Key를 직접 읽지 않는다.** 일정 간격(SampleRate)마다 `EvaluateGlobalTransform`으로 최종 자세를 샘플링.
  - 이유: Key 시각 불일치 해소 + PreRotation/PostRotation/Pivot/RotationOrder/InheritType 자동 반영
- **`Parent⁻¹ × Global`**: FbxAMatrix(열벡터) 기준. UE `FTransform`(행벡터)에서는 `Global × Parent⁻¹` (`GetRelativeTransform`).
- **Key 수 = Frame 수 + 1** (울타리 기둥). 마지막 Stop 샘플이 빠지면 루프 시 튐 → 프레임 경계 snap + `Start + i 프레임` 계산으로 해결.
- **데이터 구조 (W11, UE와 동일한 모양)**
  - `FRawAnimSequenceTrack` : `PosKeys` / `RotKeys` / `ScaleKeys` 배열 3개
  - `FBoneAnimationTrack`   : `Name` + `InternalTrack`
  - `UAnimDataModel`        : Tracks + FrameRate + PlayLength
  - `UAnimSequence`         : 클립 에셋 (`Initialize`, `GetAnimationPose`)
