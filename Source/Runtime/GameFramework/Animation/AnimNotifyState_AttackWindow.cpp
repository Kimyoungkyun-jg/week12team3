#include "EnginePCH.h"
#include "AnimNotifyState_AttackWindow.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor/MyCharacter.h"
#include "Engine/World.h"
#include "Collision/HitResult.h"

void UAnimNotifyState_AttackWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	HitActors.Reset();
}

void UAnimNotifyState_AttackWindow::NotifyTick(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	// 공격자는 아무 액터나 된다. AnimInstance 종류(MyAnimInstance, SingleNode, Lua AnimGraph)와 무관하게
	// 이 Notify가 붙은 시퀀스를 재생하는 메시의 주인이 공격한다.
	AActor* Attacker = MeshComp ? MeshComp->GetOwner() : nullptr;
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!World)
		return;

	// 캐릭터는 이동 방향을 향하므로(bOrientRotationToMovement) 액터 전방이 주먹 방향이다
	const FVector Forward = Attacker->GetActorRotation().Quaternion().GetForwardVector();
	const FRay AttackRay{ Attacker->GetActorLocation() + FVector(0.0f, 0.0f, AttackHeight), Forward };

	FHitResult Hit;
	if (!World->SweepSingle(AttackRay, AttackRange, AttackRadius, Hit, Attacker))
		return;

	AActor* HitActor = Hit.HitComponent ? Hit.HitComponent->GetOwner() : nullptr;
	if (!HitActor || HitActors.Find(HitActor) != -1)
		return;

	HitActors.Add(HitActor);

	if (AMyCharacter* AttackerCharacter = Cast<AMyCharacter>(Attacker))
	{
		// MyCharacter 공격자는 자기 DetectedHit에서 처리한다 (공격자 쪽 반응을 넣을 자리)
		AttackerCharacter->DetectedHit(Hit);
	}
	else if (AMyCharacter* Victim = Cast<AMyCharacter>(HitActor))
	{
		// 다른 액터(예: SingleNode로 펀치를 재생하는 ACharacter)가 때려도 맞은 MyCharacter는 피격 반응을 한다
		HTR_LOG(Info, "[Attack] {} -> {}", Attacker->GetName(), Victim->GetName());
		Victim->SetIsHit();
	}
}

void UAnimNotifyState_AttackWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	// 맞은 액터가 공격 사이에 사라져도 댕글링 포인터를 들고 있지 않도록 바로 비운다
	HitActors.Reset();
}
