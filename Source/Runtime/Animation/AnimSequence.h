#pragma once

#include "Animation/AnimSequenceBase.h"
#include "Animation/BonePose.h"
#include "AnimSequence.generated.h"

// 디버그: root 고정을 건너뛴다. 루트 모션은 그대로 캡슐을 움직이므로 메시가 2배로 이동하는 문제를 재현한다
// Settings > Debug Draw (PIE) > Skip Root Lock
extern bool GDebugSkipRootLock;

// Unreal ERootMotionRootLock: root를 고정할 때 어느 위치에 고정할지.
UENUM()
enum class ERootMotionRootLock : uint8
{
	RefPose,         // 스켈레톤 기본 포즈의 root 위치
	AnimFirstFrame,  // 클립 첫 프레임의 root 위치
	Zero,            // 원점
};

UCLASS()
class UAnimSequence : public UAnimSequenceBase
{
	GENERATED_BODY()

public:
	using Super::Serialize;
	void Serialize(FArchive& Ar) override;

	void Initialize(
		FString InClipName, TArray<FBoneAnimationTrack> Tracks,
		FFrameRate FrameRate, float PlayLength)
	{
		ClipName = std::move(InClipName);
		InitializeDataModel(std::move(Tracks), FrameRate, PlayLength);
	}

	// bExtractRootMotion: 호출하는 쪽이 루트 모션을 뽑는 중인가 (Unreal ExtractionContext.bExtractRootMotion).
	// root 고정 조건 = (bExtractRootMotion && bEnableRootMotion) || bForceRootLock
	void GetAnimationPose(float Time, FCompactPose& OutPose, const TArray<FBoneInfo>& RefSkeleton, bool bExtractRootMotion = true) const;
	const FString& GetClipName() const { return ClipName; }

	// 루트 모션 설정. .anim.bin(Version 5)에 저장된다. 뷰어 Details에서 바꾸고 Save로 저장한다
	bool GetEnableRootMotion() const { return bEnableRootMotion; }
	void SetEnableRootMotion(bool bEnable) { bEnableRootMotion = bEnable; }
	bool GetForceRootLock() const { return bForceRootLock; }
	void SetForceRootLock(bool bForce) { bForceRootLock = bForce; }
	ERootMotionRootLock GetRootMotionRootLock() const { return RootMotionRootLock; }
	void SetRootMotionRootLock(ERootMotionRootLock InLock) { RootMotionRootLock = InLock; }

	// StartTime부터 DeltaTime만큼 재생하는 동안 root 본이 움직인 양을 root 기준 공간으로 돌려준다. (Unreal ExtractRootMotion)
	// bAllowLooping이면 클립 끝을 넘는 구간을 [StartTime, 끝] + [처음, 나머지]로 나눠 합친다.
	FTransform ExtractRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const;

private:
	// 구간 [StartTime, EndTime]의 root 이동량. 두 시각 모두 [0, PlayLength] 안이어야 한다. (Unreal ExtractRootMotionFromRange)
	FTransform ExtractRootMotionFromRange(float StartTime, float EndTime) const;
	// 시각 Time의 root 본 로컬 Transform. root 트랙이 없으면 단위 Transform. (Unreal ExtractRootTrackTransform)
	FTransform ExtractRootTrackTransform(float Time) const;
	// 루트 모션에 쓸 root 트랙. "root" 이름을 먼저 찾고, 없으면 첫 트랙. 트랙이 없으면 nullptr.
	const FBoneAnimationTrack* FindRootTrack() const;

	FString ClipName;
	bool bEnableRootMotion = false;  // 켜면 root 이동을 추출해 Actor 이동으로 넘긴다
	bool bForceRootLock = false;     // 루트 모션 추출 여부와 관계없이 선택한 기준으로 root를 고정한다
	ERootMotionRootLock RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
};
