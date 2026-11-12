#include "EnginePCH.h"
#include "Asset/AssetUtils.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationRuntime.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace
{
	constexpr uint32 AnimSequenceMagic = 0x4D494E41; // ANIM
	constexpr uint32 AnimSequenceVersion = 5;        // 2: 미리보기 메시 경로, 3: Notify, 4: 루트 모션 설정, 5: Root Lock 기준
	constexpr int64 BytesPerBoneKey = 10 * sizeof(float);

	void SerializeName(FArchive& Ar, FString& Name)
	{
		int32 Size = static_cast<int32>(Name.size());
		Ar << Size;
		if (Ar.IsLoading())
		{
			if (Ar.IsError() || Size < 0 || Size > Ar.TotalSize() - Ar.Tell())
			{
				Ar.SetError();
				return;
			}
			Name.resize(Size);
		}
		if (Size > 0) Ar.Serialize(Name.data(), Size);
	}

	template <typename T>
	void SerializeKeys(FArchive& Ar, TArray<T>& Keys)
	{
		for (T& Key : Keys)
		{
			Ar << Key.X << Key.Y << Key.Z;
			if constexpr (std::is_same_v<T, FQuat>) Ar << Key.W;
		}
	}

	// 트랙 하나를 Frame 위치에서 보간한다. 위치는 Lerp, 회전은 Slerp (GetAnimationPose와 같은 방식)
	// Scale은 루트 모션에 쓰지 않으므로 (1,1,1) 그대로 둔다
	FTransform SampleTrack(const FRawAnimSequenceTrack& Keys, double Frame)
	{
		FTransform Result;

		auto Sample = [Frame](const auto& Array, auto& Value, auto Interpolate)
		{
			if (Array.IsEmpty())
			{
				return;
			}
			const double KeyFrame = std::clamp(Frame, 0.0, static_cast<double>(Array.Num() - 1));
			const int32 A = static_cast<int32>(KeyFrame);     // 앞 Key
			const int32 B = std::min(A + 1, Array.Num() - 1);  // 뒤 Key
			Value = Interpolate(Array[A], Array[B], static_cast<float>(KeyFrame - A));
		};

		Sample(Keys.PosKeys, Result.Location, [](const FVector& A, const FVector& B, float Alpha)
		{
			return FMath::Lerp(A, B, Alpha);
		});
		Sample(Keys.RotKeys, Result.Rotation, [](const FQuat& A, const FQuat& B, float Alpha)
		{
			return FMath::Slerp(A, B, Alpha).Normalized();
		});

		return Result;
	}
}

void UAnimSequence::Serialize(FArchive& Ar)
{
	const bool bLoading = Ar.IsLoading();
	const UAnimDataModel* Model = GetDataModel();
	if (!bLoading && !Model)
	{
		Ar.SetError();
		return;
	}

	uint32 Magic = AnimSequenceMagic;
	uint32 Version = AnimSequenceVersion;
	Ar << Magic << Version;
	if (Ar.IsError() || Magic != AnimSequenceMagic || Version < 1 || Version > AnimSequenceVersion)
	{
		Ar.SetError();
		return;
	}

	FString Name = bLoading ? FString{} : GetClipName();
	FString PreviewPath = bLoading ? FString{} : GetPreviewMeshPath();
	FFrameRate Rate = bLoading ? FFrameRate{ 0, 1 } : Model->GetFrameRate();
	float Length = bLoading ? 0.0f : Model->GetPlayLength();
	int32 TrackCount = bLoading ? 0 : Model->GetBoneAnimationTracks().Num();
	int32 KeyCount = bLoading ? 0 : Model->GetNumberOfKeys();
	SerializeName(Ar, Name);
	if (Version >= 2)
	{
		SerializeName(Ar, PreviewPath);
	}

	// 4: 루트 모션 설정. 이전 버전 파일은 둘 다 꺼진 채로 읽는다
	uint8 bRootMotion = bLoading ? 0 : static_cast<uint8>(bEnableRootMotion);
	uint8 bRootLock = bLoading ? 0 : static_cast<uint8>(bForceRootLock);
	if (Version >= 4)
	{
		Ar << bRootMotion << bRootLock;
	}
	// 이전 버전은 기존 동작인 첫 프레임 고정을 유지한다.
	uint8 RootLockMode = bLoading
		? static_cast<uint8>(ERootMotionRootLock::AnimFirstFrame)
		: static_cast<uint8>(RootMotionRootLock);
	if (Version >= 5)
	{
		Ar << RootLockMode;
	}
	if (Ar.IsError() || RootLockMode > static_cast<uint8>(ERootMotionRootLock::Zero))
	{
		Ar.SetError();
		return;
	}
	Ar << Rate.Numerator << Rate.Denominator << Length << TrackCount << KeyCount;
	if (Ar.IsError() || Rate.Numerator <= 0 || Rate.Denominator <= 0
		|| !std::isfinite(Length) || Length < 0.0f || TrackCount <= 0 || KeyCount <= 0
		|| (bLoading && TrackCount > (Ar.TotalSize() - Ar.Tell()) / (sizeof(int32) + KeyCount * BytesPerBoneKey)))
	{
		Ar.SetError();
		return;
	}

	TArray<FBoneAnimationTrack> LoadedTracks;
	if (bLoading) LoadedTracks.SetNum(TrackCount);
	for (int32 Index = 0; Index < TrackCount && !Ar.IsError(); ++Index)
	{
		if (bLoading)
		{
			auto& Track = LoadedTracks[Index];
			SerializeName(Ar, Track.Name);
			if (Ar.IsError() || KeyCount * BytesPerBoneKey > Ar.TotalSize() - Ar.Tell())
			{
				Ar.SetError();
				return;
			}
			auto& Keys = Track.InternalTrack;
			Keys.PosKeys.SetNum(KeyCount);
			Keys.RotKeys.SetNum(KeyCount);
			Keys.ScaleKeys.SetNum(KeyCount);
			SerializeKeys(Ar, Keys.PosKeys);
			SerializeKeys(Ar, Keys.RotKeys);
			SerializeKeys(Ar, Keys.ScaleKeys);
		}
		else
		{
			auto Track = Model->GetBoneAnimationTracks()[Index];
			auto& Keys = Track.InternalTrack;
			if (Keys.PosKeys.Num() != KeyCount || Keys.RotKeys.Num() != KeyCount || Keys.ScaleKeys.Num() != KeyCount)
			{
				Ar.SetError();
				return;
			}
			SerializeName(Ar, Track.Name);
			SerializeKeys(Ar, Keys.PosKeys);
			SerializeKeys(Ar, Keys.RotKeys);
			SerializeKeys(Ar, Keys.ScaleKeys);
		}
	}

	// v3: Notify. 불러올 때는 DataModel이 생긴 뒤(Initialize 뒤)에 넣어야 시간 범위를 확인할 수 있다.
	TArray<FAnimNotifyTrack> LoadedTracks_Notify;
	TArray<FAnimNotifyEvent> LoadedNotifies;
	if (Version >= 3 && !Ar.IsError())
	{
		SerializeNotifyEvents(Ar, LoadedTracks_Notify, LoadedNotifies);
	}

    if (bLoading && !Ar.IsError())
    {
        if (Ar.Tell() != Ar.TotalSize())
        {
            DeleteNotifyObjects(LoadedNotifies);
            Ar.SetError();
            return;
        }
        Initialize(std::move(Name), std::move(LoadedTracks), Rate, Length);
        SetPreviewMeshPath(std::move(PreviewPath));
        bEnableRootMotion = bRootMotion != 0;
        bForceRootLock = bRootLock != 0;
        RootMotionRootLock = static_cast<ERootMotionRootLock>(RootLockMode);
        // v2 이하 파일은 빈 목록이 들어가, 다시 읽을 때 이전 Notify가 남지 않는다
        ReplaceNotifies(std::move(LoadedTracks_Notify), std::move(LoadedNotifies));
    }
    else
    {
        DeleteNotifyObjects(LoadedNotifies);
    }
}

bool AnimSequenceFile::Read(const std::filesystem::path& Path, UAnimSequence& Sequence, FString& OutError)
{
	OutError.clear();
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "Animation binary: cannot open file or invalid size";
		return false;
	}
	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "Animation binary: cannot read file";
		return false;
	}
	FMemoryReader Reader(Bytes, true);
	Sequence.Serialize(Reader);
	if (!Reader.IsError())
	{
		Sequence.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
		return true;
	}
	OutError = "Animation binary: invalid or unsupported sequence data";
	return false;
}

bool AnimSequenceFile::Write(const std::filesystem::path& Path, UAnimSequence& Sequence, FString& OutError)
{
	OutError.clear();
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Sequence.Serialize(Writer);
	if (Writer.IsError())
	{
		OutError = "Animation binary: invalid sequence data";
		return false;
	}
	std::ofstream File(Path, std::ios::binary | std::ios::trunc);
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (File) return true;
	OutError = "Animation binary: cannot write file";
	return false;
}

bool AnimSequenceFile::IsAnimSequencePath(const FString& Path)
{
	// 파일 이름 끝이 .anim.bin인지 대소문자를 구분하지 않고 확인한다. 내용 검사는 Read가 한다
	FString End = Path.size() >= 9 ? Path.substr(Path.size() - 9) : FString();
	for (char& C : End) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
	return End == ".anim.bin";
}

FTransform UAnimSequence::ExtractRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const
{
	FRootMotionMovementParams RootMotionParams;

	if (DeltaTime != 0.f)
	{
		const bool bPlayingBackwards = (DeltaTime < 0.f);

		float PreviousPosition = StartTime;
		float CurrentPosition = StartTime;
		float DesiredDeltaMove = DeltaTime;

		do
		{
			// 루프 없이 목표 위치까지, 또는 끝(처음)까지 진행
			const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, GetPlayLength());

			// 루프 없이 [PreviousPosition, CurrentPosition] 구간의 root 이동량을 합친다
			RootMotionParams.Accumulate(ExtractRootMotionFromRange(PreviousPosition, CurrentPosition));

			// 끝에 닿았고 루프면 반대쪽 끝에서 이어감, 아니면 종료
			if ((AdvanceType == ETypeAdvanceAnim::ETAA_Finished) && bAllowLooping)
			{
				const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
				DesiredDeltaMove -= ActualDeltaMove;

				PreviousPosition = bPlayingBackwards ? GetPlayLength() : 0.f;
				CurrentPosition = PreviousPosition;
			}
			else
			{
				break;
			}
		} while (true);
	}

	return RootMotionParams.GetRootMotionTransform();
}

FTransform UAnimSequence::ExtractRootMotionFromRange(float StartTime, float EndTime) const
{
	const FTransform RootStart = ExtractRootTrackTransform(StartTime);
	const FTransform RootEnd = ExtractRootTrackTransform(EndTime);
	return RootEnd.GetRelativeTransform(RootStart);
}

FTransform UAnimSequence::ExtractRootTrackTransform(float Time) const
{
	const UAnimDataModel* Model = GetDataModel();
	const FBoneAnimationTrack* RootTrack = FindRootTrack();
	if (!Model || !RootTrack)
	{
		return FTransform();
	}

	const double Frame = static_cast<double>(FMath::Clamp(Time, 0.0f, Model->GetPlayLength())) * Model->GetFrameRate().AsDecimal();
	return SampleTrack(RootTrack->InternalTrack, Frame);
}

const FBoneAnimationTrack* UAnimSequence::FindRootTrack() const
{
	const UAnimDataModel* Model = GetDataModel();
	if (!Model)
	{
		return nullptr;
	}

	// UAL은 최상위 "Armature"가 고정이고 실제 이동은 그 아래 "root"에 있다
	const TArray<FBoneAnimationTrack>& Tracks = Model->GetBoneAnimationTracks();
	for (const FBoneAnimationTrack& Track : Tracks)
	{
		if (Track.Name == "root")
		{
			return &Track;
		}
	}

	// "root"가 없으면 첫 트랙(스켈레톤 순서상 최상위 본)을 쓴다 (Unreal: 본 인덱스 0)
	return Tracks.IsEmpty() ? nullptr : &Tracks[0];
}

bool GDebugSkipRootLock = false;

void UAnimSequence::GetAnimationPose(float Time, FCompactPose& OutPose, const TArray<FBoneInfo>& RefSkeleton, bool bExtractRootMotion) const
{
    OutPose.ResetToRefPose(RefSkeleton);
    const UAnimDataModel* Model = GetDataModel();
	if (!Model)
	{
		return;
	}

    const FFrameRate Rate = Model->GetFrameRate();
    const float Length = Model->GetPlayLength();
	if (!std::isfinite(Time) || !std::isfinite(Length) || Length < 0.0f)
	{
		return;
	}

    const double Frame = static_cast<double>(FMath::Clamp(Time, 0.0f, Length)) * Rate.AsDecimal();

    for (const FBoneAnimationTrack& Track : Model->GetBoneAnimationTracks())
    {
        int32 BoneIndex = -1;
        for (int32 Index = 0; Index < RefSkeleton.Num(); ++Index)
        {
			if (RefSkeleton[Index].Name == Track.Name)
            {
                BoneIndex = Index;
                break;
            }
        }
		if (BoneIndex < 0)
		{
			continue;
		}

        auto Sample = [Frame](const auto& Keys, auto& Value, auto Interpolate)
        {
			if (Keys.IsEmpty())
			{
				return;
			}
			const double KeyFrame = std::clamp(Frame, 0.0, static_cast<double>(Keys.Num() - 1));

            const int32 A = static_cast<int32>(KeyFrame);
            const int32 B = std::min(A + 1, Keys.Num() - 1);

            Value = Interpolate(Keys[A], Keys[B], static_cast<float>(KeyFrame - A));
        };

        FTransform& Bone = OutPose[BoneIndex];
        const auto& Keys = Track.InternalTrack;

        auto Lerp = [](const FVector& A, const FVector& B, float Alpha) 
		{ 
			return FMath::Lerp(A, B, Alpha); 
		};

        Sample(Keys.PosKeys, Bone.Location, Lerp);
        Sample(Keys.ScaleKeys, Bone.Scale, Lerp);
        Sample(Keys.RotKeys, Bone.Rotation, [](const FQuat& A, const FQuat& B, float Alpha)
        {
            return FMath::Slerp(A, B, Alpha).Normalized();
        });
    }

	// 선택한 기준으로 포즈의 root 위치·회전을 고정한다.
	// - Enable Root Motion: 이동은 ExtractRootMotion으로 Actor에 넘겼으므로 포즈에서는 뺀다
	// - Force Root Lock    : Actor로 넘기지 않아도 root를 묶어 제자리 재생한다 (RM 클립을 in-place처럼 사용)
	if (!GDebugSkipRootLock && ((bExtractRootMotion && bEnableRootMotion) || bForceRootLock))
	{
		const FBoneAnimationTrack* RootTrack = FindRootTrack();
		if (!RootTrack)
		{
			return;
		}

		for (int32 Index = 0; Index < RefSkeleton.Num(); ++Index)
		{
			if (RefSkeleton[Index].Name == RootTrack->Name)
			{
				FTransform LockTransform;
				switch (RootMotionRootLock)
				{
				case ERootMotionRootLock::RefPose:
					LockTransform = RefSkeleton[Index].LocalBindPose;
					break;
				case ERootMotionRootLock::AnimFirstFrame:
					LockTransform = SampleTrack(RootTrack->InternalTrack, 0.0);
					break;
				case ERootMotionRootLock::Zero:
					LockTransform = FTransform::Identity;
					break;
				}
				OutPose[Index].Location = LockTransform.Location;
				OutPose[Index].Rotation = LockTransform.Rotation;
				break;                                          // Scale은 애니메이션 값 그대로
			}
		}
	}
}
