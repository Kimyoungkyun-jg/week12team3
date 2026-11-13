#include "EnginePCH.h"
#include "Animation/LuaAnimInstance.h"
#include "Asset/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Asset/BlendSpace.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	UCharacterMovementComponent* MovementOf(const ULuaAnimInstance* Instance)
	{
		ACharacter* Character = Cast<ACharacter>(Instance->TryGetPawnOwner());
		return Character ? Character->GetCharacterMovement() : nullptr;
	}
}

bool ULuaAnimInstance::AddSequenceState(const FString& Name, const FString& AssetPath, bool bLoop, float PlayRate)
{
	return AddState({ Name, EPoseKind::Sequence, AssetPath, bLoop, PlayRate });
}

bool ULuaAnimInstance::AddBlendSpaceState(const FString& Name, const FString& AssetPath, bool bLoop, float PlayRate)
{
	return AddState({ Name, EPoseKind::BlendSpace, AssetPath, bLoop, PlayRate });
}

bool ULuaAnimInstance::AddState(const FStateDef& Def)
{
	if (Def.Name.empty() || !std::isfinite(Def.PlayRate) || Def.PlayRate < 0.0f) return false;
	const auto Existing = std::find_if(Defs.begin(), Defs.end(), [&](const FStateDef& D) { return D.Name == Def.Name; });
	if (Existing != Defs.end())
	{
		// 스크립트를 다시 읽어 같은 상태를 또 등록하는 경우. 내용이 같으면 그대로 둔다.
		if (Existing->Kind == Def.Kind && Existing->AssetPath == Def.AssetPath &&
			Existing->bLoop == Def.bLoop && Existing->PlayRate == Def.PlayRate) return true;
		const FStateDef Previous = *Existing;
		*Existing = Def;
		InitializeAnimation();
		if (HasState(Def.Name)) return true;
		*Existing = Previous;
		InitializeAnimation();
		return false;
	}
	if (!BuildStateNode(Def)) return false;
	Defs.push_back(Def);
	return true;
}

bool ULuaAnimInstance::BuildStateNode(const FStateDef& Def)
{
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton)
	{
		HTR_LOG(Warning, "[LuaAnim] {} : skeletal mesh is not set", Def.Name);
		return false;
	}
	if (!Machine)
	{
		Machine = CreateNode<FAnimNode_StateMachine>();
		RootNode = Machine;
	}
	FStateNodes StateNodes;
	FAnimNode_Base* Pose = nullptr;
	if (Def.Kind == EPoseKind::Sequence)
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Def.AssetPath);
		if (Sequence && Sequence->Skeleton == Mesh->Skeleton && Sequence->GetDataModel())
		{
			auto* Player = CreateNode<FAnimNode_SequencePlayer>();
			Player->Sequence = Sequence;
			Player->bLoop = Def.bLoop;
			Player->PlayRate = Def.PlayRate;
			StateNodes.Sequence = Player;
			Pose = Player;
		}
	}
	else
	{
		UBlendSpace* BlendSpace = UAssetManager::LoadBlendSpace(Def.AssetPath);
		if (BlendSpace && BlendSpace->Skeleton == Mesh->Skeleton)
		{
			auto* Player = CreateNode<FAnimNode_BlendSpacePlayer>();
			Player->BlendSpace = BlendSpace;
			Player->bLoop = Def.bLoop;
			Player->PlayRate = Def.PlayRate;
			StateNodes.BlendSpace = Player;
			Pose = Player;
		}
	}
	if (!Pose)
	{
		HTR_LOG(Warning, "[LuaAnim] {} : asset is missing or uses another skeleton: {}", Def.Name, Def.AssetPath);
		return false;
	}
	// 첫 상태는 Reset 없이 바로 재생되므로 여기서 시작 위치를 맞춘다.
	Pose->Reset();
	Machine->AddState(Def.Name, Pose);
	Nodes.push_back(StateNodes);
	return true;
}

void ULuaAnimInstance::NativeInitializeAnimation()
{
	Machine = nullptr;
	Nodes.clear();
	for (const FStateDef& Def : Defs) BuildStateNode(Def);
	bRestoreRequestedState = !RequestedState.empty();
}

void ULuaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (bRestoreRequestedState && Machine)
	{
		bRestoreRequestedState = false;
		const int32 Index = Machine->FindState(RequestedState);
		if (Index >= 0) Machine->RequestTransition(Index, 0.0f);
	}
	if (AActor* Owner = GetOwningActor())
	{
		const TArray<UActorComponent*> Components = Owner->GetComponents();
		for (UActorComponent* Component : Components)
			if (UScriptComponent* Script = Cast<UScriptComponent>(Component))
				Script->CallScript("AnimTick", DeltaSeconds);
	}
}

bool ULuaAnimInstance::HasState(const FString& Name) const
{
	return Machine && Machine->FindState(Name) >= 0;
}

bool ULuaAnimInstance::TransitionTo(const FString& Name, float BlendTime, bool bRestart)
{
	const int32 Index = Machine ? Machine->FindState(Name) : -1;
	if (Index < 0 || !Machine->RequestTransition(Index, BlendTime, bRestart)) return false;
	RequestedState = Name;
	bRestoreRequestedState = false;
	return true;
}

bool ULuaAnimInstance::SetInput(const FString& Name, float X, float Y)
{
	const FStateNodes* StateNodes = FindNodes(Name);
	if (!StateNodes || !StateNodes->BlendSpace || !std::isfinite(X) || !std::isfinite(Y)) return false;
	StateNodes->BlendSpace->InputX = X;
	StateNodes->BlendSpace->InputY = Y;
	return true;
}

FString ULuaAnimInstance::GetState() const
{
	if (!Machine) return {};
	const int32 Index = Machine->GetTargetState();
	return Machine->States.IsValidIndex(Index) ? Machine->States[Index].Name : FString{};
}

bool ULuaAnimInstance::IsInTransition() const
{
	return Machine && Machine->IsInTransition();
}

bool ULuaAnimInstance::IsFinished(const FString& Name) const
{
	const FStateNodes* StateNodes = FindNodes(Name);
	return StateNodes && StateNodes->Sequence && StateNodes->Sequence->IsFinished();
}

float ULuaAnimInstance::GetRemainingTime(const FString& Name) const
{
	const FStateNodes* StateNodes = FindNodes(Name);
	if (!StateNodes || !StateNodes->Sequence || StateNodes->Sequence->bLoop)
		return std::numeric_limits<float>::infinity();
	return StateNodes->Sequence->GetRemainingTime();
}

bool ULuaAnimInstance::IsNearEnd(const FString& Name, float Time) const
{
	const FStateNodes* StateNodes = FindNodes(Name);
	const FAnimNode_SequencePlayer* Player = StateNodes ? StateNodes->Sequence : nullptr;
	if (!Player || Player->bLoop || !Player->Sequence || !Player->Sequence->GetDataModel() || !std::isfinite(Time))
		return false;
	const float TriggerTime = std::min(Time, Player->Sequence->GetDataModel()->PlayLength * 0.5f);
	return Player->IsFinished() || Player->GetRemainingTime() <= TriggerTime;
}

const ULuaAnimInstance::FStateNodes* ULuaAnimInstance::FindNodes(const FString& Name) const
{
	const int32 Index = Machine ? Machine->FindState(Name) : -1;
	return Index >= 0 && static_cast<size_t>(Index) < Nodes.size() ? &Nodes[Index] : nullptr;
}

float ULuaAnimInstance::GetSpeed() const
{
	const UCharacterMovementComponent* Movement = MovementOf(this);
	if (!Movement) return 0.0f;
	return FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.0f).Size() * 100.0f;
}

float ULuaAnimInstance::GetDirection() const
{
	ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement) return 0.0f;
	const FVector Velocity(Movement->Velocity.X, Movement->Velocity.Y, 0.0f);
	if (Velocity.Size() * 100.0f <= 0.01f) return 0.0f;
	const FQuat Facing = Character->GetActorQuat();
	const float Forward = FVector::Dot(Velocity, Facing.GetForwardVector());
	const float Right = FVector::Dot(Velocity, Facing.GetRightVector());
	return FMath::RadiansToDegrees(std::atan2(Right, Forward));
}

bool ULuaAnimInstance::IsGrounded() const
{
	const UCharacterMovementComponent* Movement = MovementOf(this);
	return Movement && Movement->IsMovingOnGround();
}

bool ULuaAnimInstance::IsFalling() const
{
	const UCharacterMovementComponent* Movement = MovementOf(this);
	return Movement && !Movement->IsMovingOnGround() && Movement->Velocity.Z < -0.01f;
}

uint32 ULuaAnimInstance::GetJumpCounter() const
{
	const UCharacterMovementComponent* Movement = MovementOf(this);
	return Movement ? Movement->GetJumpCounter() : 0;
}
