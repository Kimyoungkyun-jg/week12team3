#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"
#include <vector>

enum class EAnimGraphInput { Constant, Speed, Direction, JumpRequested, Falling, Grounded, MovingOnGround, SequenceEnding, HitRequested, SequenceEndingGrounded, SequenceEndingAirborne, AttackRequested, AimPitch, IsAiming, DeathRequested };
enum class EAnimGraphPose { Sequence, BlendSpace1D, BlendSpace2D };
enum class EAnimGraphCompare { Always, Greater, GreaterEqual, Less, LessEqual };
enum class ERootAnimNodeType { StateMachine, Slot, LayeredBlendPerBone, AimOffset, OutputPose, SequencePlayer };

struct FRootAnimGraphNode
{
	int32 Id = 0;
	ERootAnimNodeType Type = ERootAnimNodeType::StateMachine;
	FString Name;
	float X = 0.0f;
	float Y = 0.0f;
	int32 SourceNodeId = 0;
	int32 SecondarySourceNodeId = 0;
	FString SlotName = "UpperBody";
	FString StartBoneName = "spine_01";
	bool bMeshSpaceRotationBlend = false;
	FString AimOffsetPath;
	FString SequencePath;
	float Alpha = 1.0f;
	EAnimGraphInput AlphaInput = EAnimGraphInput::Constant;
	float PlayRate = 1.0f;
	EAnimGraphInput PitchInput = EAnimGraphInput::AimPitch;
	float ConstantPitch = 0.0f;
};

struct FRootAnimGraphLink
{
	int32 FromNodeId = 0;
	int32 ToNodeId = 0;
	int32 ToPinIndex = 0;
};

struct FAnimMontageSection
{
	FString Name;
	float StartTime = 0.0f;
	float EndTime = 0.0f;
	FString NextSection;
};

struct FAnimMontageNotify
{
	FString Name;
	float Time = 0.0f;
};

struct FAnimMontageAsset
{
	FString SkeletonPath;
	FString SequencePath;
	FString SlotName = "UpperBody";
	float PlayRate = 1.0f;
	float BlendInTime = 0.1f;
	float BlendOutTime = 0.15f;
	bool bLoop = false;
	std::vector<FAnimMontageSection> Sections;
	std::vector<FAnimMontageNotify> Notifies;

	bool Validate(FString& Error) const;
	static bool Load(const FString& Path, FAnimMontageAsset& Out, FString& Error);
	bool Save(const FString& Path, FString& Error) const;
};

struct FAimOffsetSample
{
	float Pitch = 0.0f;
	FString SequencePath;
	int32 FrameIndex = 0;
};

struct FAimOffsetAsset
{
	FString SkeletonPath;
	FString BaseSequencePath;
	int32 BaseFrameIndex = 0;
	float MinPitch = -90.0f;
	float MaxPitch = 90.0f;
	std::vector<FAimOffsetSample> Samples;

	bool Validate(FString& Error) const;
	static bool Load(const FString& Path, FAimOffsetAsset& Out, FString& Error);
	bool Save(const FString& Path, FString& Error) const;
};

struct FAnimGraphPlayerNode
{
	int32 Id = 0;
	EAnimGraphPose Pose = EAnimGraphPose::Sequence;
	FString AssetPath;
	EAnimGraphInput InputX = EAnimGraphInput::Constant;
	EAnimGraphInput InputY = EAnimGraphInput::Constant;
	float ConstantX = 0.0f;
	float ConstantY = 0.0f;
	float PlayRate = 1.0f;
	bool bLoop = true;
	float X = 0.0f;
	float Y = 0.0f;
};

struct FAnimGraphState
{
	int32 Id = 0;
	FString Name;
	float X = 0.0f;
	float Y = 0.0f;
	EAnimGraphPose Pose = EAnimGraphPose::Sequence;
	FString AssetPath;
	EAnimGraphInput InputX = EAnimGraphInput::Constant;
	EAnimGraphInput InputY = EAnimGraphInput::Constant;
	float ConstantX = 0.0f;
	float ConstantY = 0.0f;
	float PlayRate = 1.0f;
	bool bLoop = true;
	bool bPoseConnected = true;
	int32 OutputPlayerId = 0;
	std::vector<FAnimGraphPlayerNode> Players;
	float PlayerOffsetX = 0.0f;
	float PlayerOffsetY = 0.0f;
	float OutputOffsetX = 0.0f;
	float OutputOffsetY = 0.0f;
	float SpeedOffsetX = 0.0f;
	float SpeedOffsetY = 0.0f;
	float DirectionOffsetX = 0.0f;
	float DirectionOffsetY = 0.0f;
};

struct FAnimGraphTransition
{
	int32 Id = 0;
	int32 From = 0;
	int32 To = 0;
	EAnimGraphInput Input = EAnimGraphInput::Speed;
	EAnimGraphCompare Compare = EAnimGraphCompare::Greater;
	float Threshold = 0.0f;
	float BlendTime = 0.1f;
};

struct FAnimGraphAsset
{
	FString SkeletonPath;
	float AnyStateX = 20.0f;
	float AnyStateY = 20.0f;
	bool bRootPoseConnected = true;
	bool bInvertDirection = false;
	int32 EntryState = 0;
	std::vector<FAnimGraphState> States;
	std::vector<FAnimGraphTransition> Transitions;
	std::vector<FRootAnimGraphNode> RootNodes;
	std::vector<FRootAnimGraphLink> RootLinks;

	void EnsureDefaultRootGraph();
	void SyncRootLinksFromNodes();
	void ApplyRootLinksToNodes();
	bool Validate(FString& Error) const;
	static bool Load(const FString& Path, FAnimGraphAsset& Out, FString& Error);
	bool Save(const FString& Path, FString& Error) const;
};
