#include "EnginePCH.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Asset/BlendSpace1D.h"
#include "Asset/BlendSpace2D.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CharacterMovementComponent.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace
{
	bool HasGraphCycle(int32 CurrentId, const std::map<int32, const FRootAnimGraphNode*>& NodeDefs, std::set<int32>& Visited)
	{
		if (CurrentId <= 0) return false;
		if (!Visited.insert(CurrentId).second) return true;
		auto It = NodeDefs.find(CurrentId);
		if (It != NodeDefs.end() && It->second)
		{
			const FRootAnimGraphNode* Def = It->second;
			if (HasGraphCycle(Def->SourceNodeId, NodeDefs, Visited)) return true;
			if (Def->Type == ERootAnimNodeType::LayeredBlendPerBone &&
				HasGraphCycle(Def->SecondarySourceNodeId, NodeDefs, Visited)) return true;
		}
		Visited.erase(CurrentId);
		return false;
	}
}

bool UAnimGraphInstance::SetPreviewGraph(const FAnimGraphAsset& InGraph)
{
	Graph = InGraph;
	bUsePreviewGraph = true;
	InitializeAnimation();
	TArray<FMatrix> Pose;
	return RootNode && EvaluatePose(Pose);
}

int32 UAnimGraphInstance::GetActiveStateId() const
{
	const int32 Index = GraphMachine ? GraphMachine->CurrentState : -1;
	return Index >= 0 && static_cast<size_t>(Index) < StateIds.size() ? StateIds[Index] : 0;
}

int32 UAnimGraphInstance::GetTargetStateId() const
{
	const int32 Index = GraphMachine ? GraphMachine->NextState : -1;
	return Index >= 0 && static_cast<size_t>(Index) < StateIds.size() ? StateIds[Index] : 0;
}

bool UAnimGraphInstance::IsStateActiveOrPending(const FString& Name) const
{
	const int32 ActiveId = GetActiveStateId();
	const int32 TargetId = GetTargetStateId();
	for (const FAnimGraphState& State : Graph.States)
		if ((State.Id == ActiveId || State.Id == TargetId) && State.Name == Name) return true;
	return false;
}

bool UAnimGraphInstance::IsStateSequenceFinished(const FString& Name) const
{
	const int32 ActiveId = GetActiveStateId();
	for (const FAnimGraphState& State : Graph.States)
		if (State.Id == ActiveId && State.Name == Name)
			for (const auto& Entry : SequencePlayers)
				if (Entry.first == ActiveId) return Entry.second && Entry.second->IsFinished();
	return false;
}

void UAnimGraphInstance::RequestHit()
{
	if (bDeathRequested || IsStateActiveOrPending("Death")) return;
	if (GraphMachine && HitStateIndex >= 0 &&
		(GraphMachine->CurrentState == HitStateIndex || GraphMachine->NextState == HitStateIndex)) return;
	bAttackRequested = false;
	bHitRequested = true;
}

void UAnimGraphInstance::RequestAttack()
{
	if (bDeathRequested || IsStateActiveOrPending("Death")) return;
	if (GraphMachine && AttackStateIndex >= 0 &&
		(GraphMachine->CurrentState == AttackStateIndex || GraphMachine->NextState == AttackStateIndex)) return;
	bAttackRequested = true;
}

void UAnimGraphInstance::RequestDeath()
{
	bHitRequested = false;
	bAttackRequested = false;
	bDeathRequested = true;
}

float UAnimGraphInstance::PlayMontage(const FString& MontagePath, float InPlayRate, const FString& StartSection)
{
	if (MontagePath.empty() || !std::isfinite(InPlayRate) || InPlayRate <= 0.0f) return 0.0f;
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton) return 0.0f;

	FAnimMontageAsset Asset;
	FString Error;
	if (!FAnimMontageAsset::Load(MontagePath, Asset, Error))
	{
		HTR_LOG(Warning, "Montage load failed: {} ({})", MontagePath, Error);
		return 0.0f;
	}
	if (!Asset.SkeletonPath.empty() && Asset.SkeletonPath != Mesh->Skeleton->GetPath())
	{
		HTR_LOG(Warning, "Montage skeleton mismatch: {}", MontagePath);
		return 0.0f;
	}
	UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Asset.SequencePath);
	if (!Sequence || Sequence->Skeleton != Mesh->Skeleton || !Sequence->GetDataModel() ||
		Sequence->GetDataModel()->PlayLength <= 0.0f)
	{
		HTR_LOG(Warning, "Montage sequence failed to load: {}", Asset.SequencePath);
		return 0.0f;
	}

	int32 SectionIdx = -1;
	float InitialPos = 0.0f;
	if (!Asset.Sections.empty())
	{
		SectionIdx = 0;
		if (!StartSection.empty())
		{
			for (size_t Idx = 0; Idx < Asset.Sections.size(); ++Idx)
			{
				if (Asset.Sections[Idx].Name == StartSection)
				{
					SectionIdx = static_cast<int32>(Idx);
					break;
				}
			}
		}
		InitialPos = std::clamp(Asset.Sections[SectionIdx].StartTime, 0.0f, Sequence->GetDataModel()->PlayLength);
	}

	FActiveMontage* Target = nullptr;
	for (auto& Existing : ActiveMontages)
	{
		if (Existing.Asset.SlotName == Asset.SlotName)
		{
			Target = &Existing;
			break;
		}
	}
	if (!Target)
	{
		ActiveMontages.push_back(FActiveMontage{});
		Target = &ActiveMontages.back();
	}

	Target->Path = MontagePath;
	Target->Asset = std::move(Asset);
	Target->Sequence = Sequence;
	Target->Position = InitialPos;
	Target->PlayRate = InPlayRate;
	Target->ElapsedTime = 0.0f;
	Target->BlendWeight = Target->Asset.BlendInTime <= 0.0f ? 1.0f : 0.0f;
	Target->CurrentSectionIndex = SectionIdx;
	Target->bStopping = false;
	Target->StopElapsed = 0.0f;
	Target->StopDuration = Target->Asset.BlendOutTime;
	Target->StopStartWeight = 1.0f;
	Target->bPlaying = true;
	Target->bNotifyIncludeStart = true;
	const float RateScale = std::isfinite(Sequence->RateScale) && Sequence->RateScale > 0.0f
		? Sequence->RateScale : 1.0f;
	return Sequence->GetDataModel()->PlayLength /
		std::max(0.001f, InPlayRate * Target->Asset.PlayRate * RateScale);
}

void UAnimGraphInstance::StopMontage(const FString& SlotName, float BlendOutOverride)
{
	for (auto& Montage : ActiveMontages)
	{
		if (!Montage.bPlaying) continue;
		if (!SlotName.empty() && Montage.Asset.SlotName != SlotName) continue;
		const float BlendOut = BlendOutOverride >= 0.0f ? BlendOutOverride : Montage.Asset.BlendOutTime;
		if (BlendOut <= 0.0f)
		{
			Montage.bPlaying = false;
			Montage.BlendWeight = 0.0f;
		}
		else if (!Montage.bStopping)
		{
			Montage.bStopping = true;
			Montage.StopElapsed = 0.0f;
			Montage.StopDuration = BlendOut;
			Montage.StopStartWeight = std::max(0.01f, Montage.BlendWeight);
		}
	}
}

bool UAnimGraphInstance::IsPlayingMontage(const FString& SlotName) const
{
	for (const auto& Montage : ActiveMontages)
	{
		if (Montage.bPlaying && (SlotName.empty() || Montage.Asset.SlotName == SlotName))
		{
			return true;
		}
	}
	return false;
}

float UAnimGraphInstance::GetMontagePosition(const FString& SlotName) const
{
	for (const auto& Montage : ActiveMontages)
	{
		if (Montage.bPlaying && (SlotName.empty() || Montage.Asset.SlotName == SlotName))
		{
			return Montage.Position;
		}
	}
	return 0.0f;
}

float UAnimGraphInstance::GetMontageWeight(const FString& SlotName) const
{
	for (const auto& Montage : ActiveMontages)
	{
		if (Montage.bPlaying && (SlotName.empty() || Montage.Asset.SlotName == SlotName))
		{
			return Montage.BlendWeight;
		}
	}
	return 0.0f;
}

void UAnimGraphInstance::UpdateMontages(float DeltaSeconds)
{
	if (!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
	for (auto& Montage : ActiveMontages)
	{
		if (!Montage.bPlaying || !Montage.Sequence || !Montage.Sequence->GetDataModel()) continue;
		const float SeqLength = Montage.Sequence->GetDataModel()->PlayLength;
		if (SeqLength <= 0.0f)
		{
			Montage.bPlaying = false;
			Montage.BlendWeight = 0.0f;
			continue;
		}
		const float RateScale = std::isfinite(Montage.Sequence->RateScale) && Montage.Sequence->RateScale > 0.0f
			? Montage.Sequence->RateScale : 1.0f;
		const float EffectiveRate = std::max(0.001f, Montage.PlayRate * Montage.Asset.PlayRate * RateScale); // montage 최종 속도 배율

		if (Montage.bStopping)
		{
			Montage.StopElapsed += DeltaSeconds;
			if (Montage.StopDuration <= 0.0f || Montage.StopElapsed >= Montage.StopDuration)
			{
				Montage.bPlaying = false;
				Montage.BlendWeight = 0.0f;
				continue;
			}
			Montage.BlendWeight = Montage.StopStartWeight *
				(1.0f - std::clamp(Montage.StopElapsed / Montage.StopDuration, 0.0f, 1.0f));
		}

		float SecStart = 0.0f;
		float SecEnd = SeqLength;
		FString NextSectionName;
		if (Montage.CurrentSectionIndex >= 0 &&
			static_cast<size_t>(Montage.CurrentSectionIndex) < Montage.Asset.Sections.size())
		{
			const FAnimMontageSection& Sec = Montage.Asset.Sections[Montage.CurrentSectionIndex];
			SecStart = std::clamp(Sec.StartTime, 0.0f, SeqLength);
			SecEnd = Sec.EndTime > SecStart ? std::min(Sec.EndTime, SeqLength) : SeqLength;
			NextSectionName = Sec.NextSection;
		}

		const float PrevPos = Montage.Position;
		const float NextPos = PrevPos + DeltaSeconds * EffectiveRate;
		const bool bFirstStep = (Montage.ElapsedTime <= 1.e-6f);
		Montage.ElapsedTime += DeltaSeconds;

		const float NotifyCheckEnd = std::min(NextPos, SecEnd);
		TArray<FAnimNotifyQueueItem> SequenceNotifies;
		Montage.Sequence->GetNotifyQueueItems(PrevPos, NotifyCheckEnd, 0,
			Montage.bNotifyIncludeStart, 1.0f, SequenceNotifies);
		Montage.bNotifyIncludeStart = false;
		for (FAnimNotifyQueueItem& Item : SequenceNotifies)
		{
			Item.SourceNode = Montage.Sequence;
			LastTriggeredNotify = Item.Event.Name;
			if (!Item.Event.IsState() && OnMontageNotify) OnMontageNotify(Item.Event.Name);
		}
		AddNotifyQueueItems(SequenceNotifies);
		for (const auto& Notify : Montage.Asset.Notifies)
		{
			const bool bAlreadyTriggered = std::any_of(SequenceNotifies.begin(), SequenceNotifies.end(),
				[&](const FAnimNotifyQueueItem& Item)
				{
					return !Item.Event.IsState() && Item.Event.Name == Notify.Name &&
						std::abs(Item.Event.TriggerTime - Notify.Time) < 0.001f;
				});
			const bool bTriggered = bFirstStep
				? (Notify.Time >= PrevPos && Notify.Time <= NotifyCheckEnd)
				: (Notify.Time > PrevPos && Notify.Time <= NotifyCheckEnd);
			if (bTriggered && !bAlreadyTriggered)
			{
				LastTriggeredNotify = Notify.Name;
				if (OnMontageNotify) OnMontageNotify(Notify.Name);
			}
		}

		if (NextPos < SecEnd)
		{
			Montage.Position = NextPos;
		}
		else
		{
			int32 NextSecIdx = -1;
			if (!NextSectionName.empty())
			{
				for (size_t Idx = 0; Idx < Montage.Asset.Sections.size(); ++Idx)
				{
					if (Montage.Asset.Sections[Idx].Name == NextSectionName)
					{
						NextSecIdx = static_cast<int32>(Idx);
						break;
					}
				}
			}
			if (NextSecIdx >= 0)
			{
				Montage.CurrentSectionIndex = NextSecIdx;
				Montage.Position = std::clamp(Montage.Asset.Sections[NextSecIdx].StartTime, 0.0f, SeqLength);
				Montage.bNotifyIncludeStart = true;
			}
			else if (Montage.Asset.bLoop)
			{
				const float Span = std::max(0.001f, SecEnd - SecStart);
				Montage.Position = SecStart + std::fmod(NextPos - SecStart, Span);
				Montage.bNotifyIncludeStart = true;
			}
			else
			{
				Montage.Position = SecEnd;
				Montage.bPlaying = false;
				Montage.BlendWeight = 0.0f;
				continue;
			}
		}

		if (!Montage.bStopping)
		{
			const float BlendInAlpha = Montage.Asset.BlendInTime > 0.0f
				? std::clamp(Montage.ElapsedTime / Montage.Asset.BlendInTime, 0.0f, 1.0f) : 1.0f;
			const float RemainingSeconds = std::max(0.0f, SecEnd - Montage.Position) / EffectiveRate;
			const bool bWillEnd = !Montage.Asset.bLoop && NextSectionName.empty();
			const float BlendOutAlpha = (bWillEnd && Montage.Asset.BlendOutTime > 0.0f)
				? std::clamp(RemainingSeconds / Montage.Asset.BlendOutTime, 0.0f, 1.0f) : 1.0f;
			Montage.BlendWeight = std::min(BlendInAlpha, BlendOutAlpha);
		}
	}
}

bool UAnimGraphInstance::EvaluateSlotPose(const FString& SlotName, const USkeleton* Skeleton, TArray<FAnimBoneTransform>& InOutPose)
{
	if (!Skeleton) return false;
	for (const auto& Montage : ActiveMontages)
	{
		if (!Montage.bPlaying || Montage.Asset.SlotName != SlotName || Montage.BlendWeight <= 0.0f) continue;
		if (!Montage.Sequence || Montage.Sequence->Skeleton != Skeleton) continue;
		TArray<FAnimBoneTransform> MontagePose;
		if (!Montage.Sequence->EvaluateLocalPoseTransforms(Montage.Position, MontagePose)) continue;
		if (Montage.BlendWeight >= 1.0f || InOutPose.Num() != MontagePose.Num())
		{
			InOutPose = std::move(MontagePose);
			return true;
		}
		TArray<FAnimBoneTransform> Blended;
		if (BlendAnimPoses(InOutPose, MontagePose, Montage.BlendWeight, Blended))
		{
			InOutPose = std::move(Blended);
			return true;
		}
	}
	return false;
}

float UAnimGraphInstance::InputValue(EAnimGraphInput Input, float Constant) const
{
	switch (Input)
	{
	case EAnimGraphInput::Speed: return Speed;
	case EAnimGraphInput::Direction: return Direction;
	case EAnimGraphInput::JumpRequested: return bJumpRequested ? 1.0f : 0.0f;
	case EAnimGraphInput::Falling: return bFalling ? 1.0f : 0.0f;
	case EAnimGraphInput::Grounded: return bGrounded ? 1.0f : 0.0f;
	case EAnimGraphInput::MovingOnGround: return bMovingOnGround ? 1.0f : 0.0f;
	case EAnimGraphInput::HitRequested: return bHitRequested ? 1.0f : 0.0f;
	case EAnimGraphInput::AttackRequested: return bAttackRequested ? 1.0f : 0.0f;
	case EAnimGraphInput::DeathRequested: return bDeathRequested ? 1.0f : 0.0f;
	case EAnimGraphInput::AimPitch: return AimPitch;
	case EAnimGraphInput::IsAiming: return bIsAiming ? 1.0f : 0.0f;
	default: return Constant;
	}
}

void UAnimGraphInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (std::isfinite(DeltaSeconds) && DeltaSeconds > 0.0f)
		AimBlendAlpha = std::clamp(AimBlendAlpha + (bIsAiming ? 1.0f : -1.0f) * DeltaSeconds / 0.18f, 0.0f, 1.0f);
	Speed = Direction = 0.0f;
	bJumpRequested |= bManualJumpRequested;
	bManualJumpRequested = false;
	bFalling = bGrounded = bMovingOnGround = false;
	ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement)
	{
		const uint32 JumpCounter = Movement->GetJumpCounter();
		bJumpRequested |= JumpCounter != LastJumpCounter;
		LastJumpCounter = JumpCounter;
		bGrounded = Movement->IsMovingOnGround();
		bFalling = !bGrounded && Movement->Velocity.Z < -0.01f;
		const FVector Velocity(Movement->Velocity.X, Movement->Velocity.Y, 0.0f);
		Speed = Velocity.Size() * 100.0f;
		bMovingOnGround = bGrounded && Velocity.SizeSquared() > 0.09f;
		if (Speed > 0.01f)
		{
			const FQuat Facing = Character->GetActorQuat();
			const float Forward = FVector::Dot(Velocity, Facing.GetForwardVector());
			const float Right = FVector::Dot(Velocity, Facing.GetRightVector());
			Direction = FMath::RadiansToDegrees(std::atan2(Right, Forward));
			if (Graph.bInvertDirection) Direction = -Direction;
		}
	}
	if (Character && !bPreviewAimPitchOverride)
	{
		if (USpringArmComponent* SpringArm = Character->GetSpringArmComponent())
		{
			// 스프링암 상대 피치는 위쪽 시선에서 음수이므로 부호를 반전해 조준 각도로 전달한다
			AimPitch = std::clamp(-SpringArm->GetRelativeRotation().Pitch, -90.0f, 90.0f);
		}
	}
	else if (bPreviewAimPitchOverride)
	{
		AimPitch = PreviewAimPitch;
	}
	if (bPreviewMovementOverride)
	{
		bFalling = bPreviewFalling;
		bGrounded = !bPreviewFalling;
		bMovingOnGround = bGrounded && Speed > 30.0f;
	}
	for (const FInputBinding& Binding : Bindings)
	{
		Binding.Player->InputX = InputValue(Binding.X, Binding.ConstantX);
		Binding.Player->InputY = InputValue(Binding.Y, Binding.ConstantY);
	}
	for (const FAimOffsetBinding& AimBinding : AimOffsetBindings)
	{
		if (AimBinding.Node)
		{
			AimBinding.Node->Pitch = InputValue(AimBinding.PitchInput, AimBinding.ConstantPitch);
		}
	}
	for (const FAlphaBinding& Binding : AlphaBindings)
		if (Binding.Alpha) *Binding.Alpha = Binding.Input == EAnimGraphInput::IsAiming
			? AimBlendAlpha : InputValue(Binding.Input);
	UpdateMontages(DeltaSeconds);
}

void UAnimGraphInstance::BuildGraph()
{
	Bindings.clear();
	AimOffsetBindings.clear();
	AlphaBindings.clear();
	StateIds.clear();
	SequencePlayers.clear();
	GraphMachine = nullptr;
	HitStateIndex = AttackStateIndex = -1;
	bJumpRequested = bManualJumpRequested = false;
	bHitRequested = bAttackRequested = bDeathRequested = false;
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Component || !Mesh || !Mesh->Skeleton || (!bUsePreviewGraph && Component->GetAnimGraphPath().empty())) return;
	if (!bUsePreviewGraph)
	{
		FString Error;
		if (!FAnimGraphAsset::Load(Component->GetAnimGraphPath(), Graph, Error))
		{ HTR_LOG(Warning, "AnimGraph load failed: {}", Error); return; }
	}
	Graph.EnsureDefaultRootGraph();
	if (!Graph.SkeletonPath.empty() && Graph.SkeletonPath != Mesh->Skeleton->GetPath())
	{ HTR_LOG(Warning, "AnimGraph skeleton does not match mesh: {}", Component->GetAnimGraphPath()); return; }
	if (!Graph.bRootPoseConnected)
	{ HTR_LOG(Warning, "AnimGraph root Output Pose is disconnected: {}", Component->GetAnimGraphPath()); return; }
	if (ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner()))
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			LastJumpCounter = Movement->GetJumpCounter();
	GraphMachine = CreateNode<FAnimNode_StateMachine>();
	std::vector<const FAnimGraphState*> Ordered;
	for (const auto& State : Graph.States) if (State.Id == Graph.EntryState) Ordered.push_back(&State);
	for (const auto& State : Graph.States) if (State.Id != Graph.EntryState) Ordered.push_back(&State);
	for (const FAnimGraphState* State : Ordered)
	{
		if (!State->bPoseConnected)
		{ HTR_LOG(Warning, "AnimGraph state has no Output Pose connection: {}", State->Name); GraphMachine = nullptr; return; }
		FAnimGraphPlayerNode PlayerData;
		if (State->OutputPlayerId == 0)
		{
			PlayerData.Pose = State->Pose; PlayerData.AssetPath = State->AssetPath;
			PlayerData.InputX = State->InputX; PlayerData.InputY = State->InputY;
			PlayerData.ConstantX = State->ConstantX; PlayerData.ConstantY = State->ConstantY;
			PlayerData.PlayRate = State->PlayRate; PlayerData.bLoop = State->bLoop;
		}
		else
		{
			const auto It = std::find_if(State->Players.begin(), State->Players.end(),
				[&](const auto& Player) { return Player.Id == State->OutputPlayerId; });
			if (It == State->Players.end()) { GraphMachine = nullptr; return; }
			PlayerData = *It;
		}
		FAnimNode_Base* Pose = nullptr;
		if (PlayerData.Pose == EAnimGraphPose::Sequence)
		{
			UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(PlayerData.AssetPath);
			if (!Sequence) HTR_LOG(Warning, "AnimGraph sequence failed to load: {}", PlayerData.AssetPath);
			else if (Sequence->Skeleton != Mesh->Skeleton)
				HTR_LOG(Warning, "AnimGraph skeleton mismatch: sequence {} uses {}, mesh uses {}",
					PlayerData.AssetPath, Sequence->Skeleton ? Sequence->Skeleton->GetPath() : "None",
					Mesh->Skeleton->GetPath());
			if (Sequence && Sequence->Skeleton == Mesh->Skeleton && Sequence->GetDataModel())
			{
				auto* Player = CreateNode<FAnimNode_SequencePlayer>();
				Player->Sequence = Sequence; Player->bLoop = PlayerData.bLoop; Player->PlayRate = PlayerData.PlayRate;
				Pose = Player;
				SequencePlayers.push_back({ State->Id, Player });
			}
		}
		else
		{
			UBlendSpace* BlendSpace = UAssetManager::LoadBlendSpace(PlayerData.AssetPath);
			const bool CorrectType = PlayerData.Pose == EAnimGraphPose::BlendSpace1D
				? BlendSpace && BlendSpace->IsA<UBlendSpace1D>() : BlendSpace && BlendSpace->IsA<UBlendSpace2D>();
			if (CorrectType && BlendSpace->Skeleton == Mesh->Skeleton)
			{
				auto* Player = CreateNode<FAnimNode_BlendSpacePlayer>();
				Player->BlendSpace = BlendSpace; Player->bLoop = PlayerData.bLoop; Player->PlayRate = PlayerData.PlayRate;
				Bindings.push_back({ Player, PlayerData.InputX, PlayerData.InputY, PlayerData.ConstantX, PlayerData.ConstantY });
				Pose = Player;
			}
		}
		if (!Pose)
		{ HTR_LOG(Warning, "AnimGraph state asset is missing or incompatible: {}", PlayerData.AssetPath); GraphMachine = nullptr; return; }
		GraphMachine->AddState(State->Name, Pose);
		StateIds.push_back(State->Id);
	}
	for (const auto& Transition : Graph.Transitions)
	{
		auto From = std::find(StateIds.begin(), StateIds.end(), Transition.From);
		auto To = std::find(StateIds.begin(), StateIds.end(), Transition.To);
		if ((Transition.From != -1 && From == StateIds.end()) || To == StateIds.end()) continue;
		if (Transition.From == -1 && Transition.Input == EAnimGraphInput::HitRequested)
			HitStateIndex = static_cast<int32>(To - StateIds.begin());
		if (Transition.Input == EAnimGraphInput::AttackRequested)
			AttackStateIndex = static_cast<int32>(To - StateIds.begin());
		FAnimNode_SequencePlayer* FromPlayer = nullptr;
		for (const auto& Entry : SequencePlayers) if (Entry.first == Transition.From) { FromPlayer = Entry.second; break; }
		GraphMachine->AddTransition(Transition.From == -1 ? -1 : static_cast<int32>(From - StateIds.begin()),
			static_cast<int32>(To - StateIds.begin()),
			[this, Transition, FromPlayer]
			{
			if (Transition.Input == EAnimGraphInput::SequenceEnding ||
				Transition.Input == EAnimGraphInput::SequenceEndingGrounded ||
				Transition.Input == EAnimGraphInput::SequenceEndingAirborne)
			{
				if (!FromPlayer || !FromPlayer->Sequence || !FromPlayer->Sequence->GetDataModel()) return false;
				const float TriggerTime = std::min(Transition.Threshold,
					FromPlayer->Sequence->GetDataModel()->PlayLength * 0.5f);
				const bool bEnding = FromPlayer->IsFinished() || FromPlayer->GetRemainingTime() <= TriggerTime;
				return bEnding && (Transition.Input == EAnimGraphInput::SequenceEnding ||
					(Transition.Input == EAnimGraphInput::SequenceEndingGrounded && bGrounded) ||
					(Transition.Input == EAnimGraphInput::SequenceEndingAirborne && !bGrounded));
			}
			const float Value = InputValue(Transition.Input);
			switch (Transition.Compare)
			{
			case EAnimGraphCompare::Always: return true;
			case EAnimGraphCompare::Greater: return Value > Transition.Threshold;
			case EAnimGraphCompare::GreaterEqual: return Value >= Transition.Threshold;
			case EAnimGraphCompare::Less: return Value < Transition.Threshold;
			case EAnimGraphCompare::LessEqual: return Value <= Transition.Threshold;
			}
			return false;
			}, Transition.BlendTime, {}, Transition.Input == EAnimGraphInput::JumpRequested
				? std::function<void()>([this] { bJumpRequested = false; }) :
				Transition.Input == EAnimGraphInput::HitRequested
				? std::function<void()>([this] { bHitRequested = false; }) :
				Transition.Input == EAnimGraphInput::AttackRequested
				? std::function<void()>([this] { bAttackRequested = false; }) :
				Transition.Input == EAnimGraphInput::DeathRequested
				? std::function<void()>([this] { bDeathRequested = false; }) : std::function<void()>{});
	}

	// 루트 포즈 그래프 노드를 생성하고 핀 연결을 구성한다
	std::map<int32, const FRootAnimGraphNode*> RootNodeDefs;
	std::map<int32, FAnimNode_Base*> RuntimeNodes;
	std::map<int32, int32> InputRefCount;
	const FRootAnimGraphNode* OutputNodeDef = nullptr;

	for (const auto& NodeDef : Graph.RootNodes)
	{
		RootNodeDefs[NodeDef.Id] = &NodeDef;
		if (NodeDef.SourceNodeId > 0) InputRefCount[NodeDef.SourceNodeId]++;
		if (NodeDef.Type == ERootAnimNodeType::LayeredBlendPerBone && NodeDef.SecondarySourceNodeId > 0)
			InputRefCount[NodeDef.SecondarySourceNodeId]++;

		if (NodeDef.Type == ERootAnimNodeType::StateMachine)
		{
			RuntimeNodes[NodeDef.Id] = GraphMachine;
		}
		else if (NodeDef.Type == ERootAnimNodeType::SequencePlayer)
		{
			UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(NodeDef.SequencePath);
			if (!Sequence || Sequence->Skeleton != Mesh->Skeleton || !Sequence->GetDataModel())
			{
				HTR_LOG(Warning, "AnimGraph root sequence is missing or incompatible: {}", NodeDef.SequencePath);
				continue;
			}
			auto* Player = CreateNode<FAnimNode_SequencePlayer>();
			Player->Sequence = Sequence;
			Player->PlayRate = NodeDef.PlayRate;
			RuntimeNodes[NodeDef.Id] = Player;
		}
		else if (NodeDef.Type == ERootAnimNodeType::Slot)
		{
			auto* SlotNode = CreateNode<FAnimNode_Slot>();
			SlotNode->SlotName = NodeDef.SlotName.empty() ? "UpperBody" : NodeDef.SlotName;
			SlotNode->GraphInstance = this;
			RuntimeNodes[NodeDef.Id] = SlotNode;
		}
		else if (NodeDef.Type == ERootAnimNodeType::LayeredBlendPerBone)
		{
			auto* BlendNode = CreateNode<FAnimNode_LayeredBoneBlend>();
			BlendNode->StartBoneName = NodeDef.StartBoneName.empty() ? "spine_01" : NodeDef.StartBoneName;
			BlendNode->Alpha = NodeDef.Alpha;
			BlendNode->bMeshSpaceRotationBlend = NodeDef.bMeshSpaceRotationBlend;
			if (NodeDef.AlphaInput != EAnimGraphInput::Constant)
				AlphaBindings.push_back({ &BlendNode->Alpha, NodeDef.AlphaInput });
			RuntimeNodes[NodeDef.Id] = BlendNode;
		}
		else if (NodeDef.Type == ERootAnimNodeType::AimOffset)
		{
			auto* AimNode = CreateNode<FAnimNode_AimOffset1D>();
			AimNode->Alpha = NodeDef.Alpha;
			if (NodeDef.AlphaInput != EAnimGraphInput::Constant)
				AlphaBindings.push_back({ &AimNode->Alpha, NodeDef.AlphaInput });
			if (!NodeDef.AimOffsetPath.empty())
			{
				FString AimError;
				if (FAimOffsetAsset::Load(NodeDef.AimOffsetPath, AimNode->Asset, AimError))
				{
					if (!AimNode->Asset.BaseSequencePath.empty())
					{
						AimNode->BaseSequence = UAssetManager::LoadAnimSequence(AimNode->Asset.BaseSequencePath);
					}
					std::vector<FAimOffsetSample> SortedSamples = AimNode->Asset.Samples;
					std::sort(SortedSamples.begin(), SortedSamples.end(),
						[](const FAimOffsetSample& Left, const FAimOffsetSample& Right) { return Left.Pitch < Right.Pitch; });
					for (const auto& Sample : SortedSamples)
					{
						UAnimSequence* Seq = UAssetManager::LoadAnimSequence(Sample.SequencePath);
						if (Seq && Seq->Skeleton == Mesh->Skeleton && Seq->GetDataModel())
						{
							AimNode->Samples.Add({ Sample.Pitch, Seq, Sample.FrameIndex });
						}
					}
				}
			}
			AimOffsetBindings.push_back({ AimNode, NodeDef.PitchInput, NodeDef.ConstantPitch });
			RuntimeNodes[NodeDef.Id] = AimNode;
		}
		else if (NodeDef.Type == ERootAnimNodeType::OutputPose)
		{
			OutputNodeDef = &NodeDef;
		}
	}

	auto ResolveRuntimeNode = [&](int32 TargetId) -> FAnimNode_Base*
	{
		if (TargetId <= 0) return nullptr;
		std::set<int32> Visited;
		if (HasGraphCycle(TargetId, RootNodeDefs, Visited)) return nullptr;
		auto It = RuntimeNodes.find(TargetId);
		return It != RuntimeNodes.end() ? It->second : nullptr;
	};

	for (const auto& NodeDef : Graph.RootNodes)
	{
		FAnimNode_Base* Runtime = ResolveRuntimeNode(NodeDef.Id);
		if (!Runtime) continue;
		if (NodeDef.Type == ERootAnimNodeType::Slot)
		{
			auto* SlotNode = static_cast<FAnimNode_Slot*>(Runtime);
			SlotNode->Source = ResolveRuntimeNode(NodeDef.SourceNodeId);
			SlotNode->bGraphManaged = (InputRefCount[NodeDef.SourceNodeId] > 1);
		}
		else if (NodeDef.Type == ERootAnimNodeType::LayeredBlendPerBone)
		{
			auto* BlendNode = static_cast<FAnimNode_LayeredBoneBlend*>(Runtime);
			BlendNode->BasePose = ResolveRuntimeNode(NodeDef.SourceNodeId);
			BlendNode->BlendPose = ResolveRuntimeNode(NodeDef.SecondarySourceNodeId);
		}
		else if (NodeDef.Type == ERootAnimNodeType::AimOffset)
		{
			auto* AimNode = static_cast<FAnimNode_AimOffset1D*>(Runtime);
			AimNode->BasePose = ResolveRuntimeNode(NodeDef.SourceNodeId);
		}
	}

	if (OutputNodeDef)
	{
		RootNode = ResolveRuntimeNode(OutputNodeDef->SourceNodeId);
	}
	else
	{
		RootNode = GraphMachine;
	}
}

void UAnimGraphInstance::NativeInitializeAnimation()
{
	BuildGraph();
}

int32 UAnimGraphInstance::AddSequenceState(FAnimNode_StateMachine& Machine, const FString& Name,
	const FString& AssetPath, bool bLoop, FAnimNode_SequencePlayer*& OutPlayer)
{
	OutPlayer = nullptr;
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton) return -1;
	UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(AssetPath);
	if (!Sequence || Sequence->Skeleton != Mesh->Skeleton || !Sequence->GetDataModel()) return -1;
	OutPlayer = CreateNode<FAnimNode_SequencePlayer>();
	OutPlayer->Sequence = Sequence;
	OutPlayer->bLoop = bLoop;
	return Machine.AddState(Name, OutPlayer);
}
