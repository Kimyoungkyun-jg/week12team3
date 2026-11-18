#include "EnginePCH.h"
#include "Animation/AnimNotify.h"
#include "Animation/AnimInstance.h"
#include "Asset/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "UObject/Casts.h"
#include "Engine/Classes/Sound/SoundWave.h"
#include "Engine/GameplayStatics.h"
#include "Engine/World.h"
#include "Render/SkeletalMesh.h"

void UAnimNotify_Log::Notify(const FAnimNotifyContext& Context)
{
	const FString SequencePath = Context.Sequence ? Context.Sequence->GetPath() : FString("None");
	const FString EventName = Context.Event ? Context.Event->Name : FString("None");
	const float TriggerTime = Context.Event ? Context.Event->TriggerTime : 0.0f;
	HTR_LOG(Info, "[AnimNotify] {} '{}' @ {:.3f}s weight {:.2f} ({}) {}", GetClass()->Name, EventName, TriggerTime, Context.Weight, SequencePath, Message);
}

void UAnimNotify_PlaySound::Notify(const FAnimNotifyContext& Context)
{
	USkeletalMeshComponent* Mesh = Context.MeshComponent;
	if (!Mesh || SoundPath.empty())
	{
		return;
	}

	if (SoundPath == FailedSoundPath)
	{
		return;
	}
	USoundWave* Sound = UAssetManager::Get().LoadSound(SoundPath);
	if (!Sound)
	{
		FailedSoundPath = SoundPath;
		HTR_LOG(Warning, "[AnimNotify] {}: cannot load sound '{}'", GetClass()->Name, SoundPath);
		return;
	}

	// 기본 위치는 컴포넌트 원점. 본이 지정되어 있고 찾으면 그 본의 월드 위치를 쓴다.
	// 본 변환은 컴포넌트 공간 기준이고 행 벡터라 (본 -> 컴포넌트 -> 월드) 순서로 곱한다.
	FVector Location = Mesh->GetWorldLocation();
	if (!BoneName.empty() && Mesh->GetSkeletalMesh())
	{
		if (Mesh->DoesSocketExist(BoneName))
		{
			Location = Mesh->GetSocketWorldMatrix(BoneName).TransformPosition(FVector(0.f, 0.f, 0.f));
		}
		else if (WarnedBoneName != BoneName)
		{
			WarnedBoneName = BoneName;
			HTR_LOG(Warning, "[AnimNotify] {}: bone '{}' not found, playing at the component location", GetClass()->Name, BoneName);
		}
	}

	const UWorld* World = Mesh->GetWorld();
	if (World && World->IsGameWorld())
	{
		UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, Volume * Context.Weight, Pitch);
	}
	else
	{
		// 에디터 프리뷰 월드에는 오디오 리스너가 없다 (PIE에서만 설정된다). 3D로 재생하면 리스너가 다른 월드라 소리가 0이 되므로,
		// 위치와 거리 감쇠 없이 2D로 재생한다. 프리뷰가 멈춰도 끝까지 들리도록 UI 사운드로 재생한다.
		UGameplayStatics::PlaySound2D(World, Sound, Volume * Context.Weight, Pitch, 0.0f, true);
	}
}

namespace
{
FString DescribeStateEvent(const FAnimNotifyContext& Context)
{
	const FString SequencePath = Context.Sequence ? Context.Sequence->GetPath() : FString("None");
	const FString EventName = Context.Event ? Context.Event->Name : FString("None");
	const float Start = Context.Event ? Context.Event->TriggerTime : 0.0f;
	const float Duration = Context.Event ? Context.Event->Duration : 0.0f;
	return std::format("'{}' {:.3f}s~{:.3f}s weight {:.2f} ({})", EventName, Start, Start + Duration, Context.Weight, SequencePath);
}
}

void UAnimNotifyState_Log::NotifyBegin(const FAnimNotifyContext& Context)
{
	HTR_LOG(Info, "[AnimNotifyState] Begin {} {}", DescribeStateEvent(Context), Message);
}

void UAnimNotifyState_Log::NotifyTick(const FAnimNotifyContext& Context)
{
	if (bLogTick)
	{
		HTR_LOG(Info, "[AnimNotifyState] Tick  {} dt {:.4f}", DescribeStateEvent(Context), Context.DeltaTime);
	}
}

void UAnimNotifyState_Log::NotifyEnd(const FAnimNotifyContext& Context)
{
	HTR_LOG(Info, "[AnimNotifyState] End   {} {}{}", DescribeStateEvent(Context), Message, Context.bInterrupted ? " (interrupted)" : "");
}

namespace
{
	UParticleSystemComponent* FindTargetParticleComponent(USkeletalMeshComponent* Mesh, const FString& ParticlePath, bool bAllowFallback, bool bCreateIfMissing)
	{
		AActor* Owner = Mesh ? Mesh->GetOwner() : nullptr;
		if (!Owner)
			return nullptr;

		UParticleSystemComponent* Matched = nullptr;
		UParticleSystemComponent* Fallback = nullptr;
		for (UActorComponent* Comp : Owner->GetComponents())
		{
			UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Comp);
			if (!ParticleComp)
				continue;
			const bool bIsDeathComp = ParticleComp->GetName() == "DeathParticleComponent";
			if (!ParticlePath.empty() && (ParticleComp->TemplatePath == ParticlePath ||
				ParticleComp->TemplatePath == "Assets/" + ParticlePath))
			{
				// 시작 시에는 비활성 컴포넌트를 우선 재사용하고 틱이나 종료 시에는 활성 컴포넌트를 찾는다
				if (bCreateIfMissing)
				{
					if (!ParticleComp->IsActive())
						return ParticleComp;
				}
				else
				{
					if (!Matched || (!Matched->IsActive() && ParticleComp->IsActive()))
						Matched = ParticleComp;
				}
			}
			if (bAllowFallback && !bIsDeathComp && !ParticleComp->IsActive() && !Fallback)
				Fallback = ParticleComp;
		}
		if (Matched)
			return Matched;
		if (bAllowFallback && Fallback)
			return Fallback;
		if (bCreateIfMissing)
		{
			if (UParticleSystemComponent* Created = Cast<UParticleSystemComponent>(
				Owner->AddComponent(UParticleSystemComponent::StaticClass(), FName("None"))))
			{
				Created->SetupAttachment(Mesh);
				Created->bAutoDestroy = false;
				Created->bPreviewInEditor = false;
				Created->SetAutoActivate(false);
				Created->SetVisibility(false);
				Created->Deactivate();
				return Created;
			}
		}
		return nullptr;
	}

	UParticleSystemComponent* FindTrackedParticleComponent(USkeletalMeshComponent* Mesh,
		TMap<USkeletalMeshComponent*, UParticleSystemComponent*>& ActiveComponents, const FString& ParticlePath)
	{
		AActor* Owner = Mesh ? Mesh->GetOwner() : nullptr;
		if (!Owner)
			return nullptr;
		if (UParticleSystemComponent** Tracked = ActiveComponents.Find(Mesh))
		{
			for (UActorComponent* Comp : Owner->GetComponents())
			{
				if (Comp == *Tracked)
					return *Tracked;
			}
		}
		return FindTargetParticleComponent(Mesh, ParticlePath, false, false);
	}

	FRotator ExtractRotationFromMatrix(const FMatrix& Matrix)
	{
		FMatrix RotMat = FMatrix::Identity;
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			FVector Row(Matrix.M[Axis][0], Matrix.M[Axis][1], Matrix.M[Axis][2]);
			const float LenSq = Row.Dot(Row);
			if (LenSq > 1e-8f)
				Row = Row / std::sqrt(LenSq);
			RotMat.M[Axis][0] = Row.X;
			RotMat.M[Axis][1] = Row.Y;
			RotMat.M[Axis][2] = Row.Z;
		}
		const float Horizontal = std::sqrt(RotMat.M[0][0] * RotMat.M[0][0] + RotMat.M[0][1] * RotMat.M[0][1]);
		if (Horizontal > 1e-6f)
			return MatrixToRotator(RotMat);
		return FRotator(FMath::RadiansToDegrees(std::atan2(-RotMat.M[0][2], Horizontal)),
			FMath::RadiansToDegrees(std::atan2(-RotMat.M[1][0], RotMat.M[1][1])), 0.0f);
	}

	void SyncParticleToBone(USkeletalMeshComponent* Mesh, UParticleSystemComponent* ParticleComp,
		const FString& BoneName, const FString& SecondBoneName,
		const FString& BaseParamName, const FString& TipParamName,
		const FVector& BoneOffset, bool bFollowBoneRotation)
	{
		if (!Mesh || !ParticleComp || !Mesh->GetSkeletalMesh())
			return;

		if (BoneName.empty())
		{
			ParticleComp->SetWorldLocationAndRotation(Mesh->GetWorldLocation(), Mesh->GetWorldRotation(), false);
			return;
		}

		if (!Mesh->DoesSocketExist(BoneName))
			return;

		const FMatrix BoneWorld = Mesh->GetSocketWorldMatrix(BoneName);
		const FVector BonePos = BoneWorld.TransformPosition(BoneOffset);
		if (!SecondBoneName.empty() && Mesh->DoesSocketExist(SecondBoneName))
		{
			const FVector SecondPos = Mesh->GetSocketWorldMatrix(SecondBoneName).TransformPosition(FVector::ZeroVector);
			const FVector MidPos = (BonePos + SecondPos) * 0.5f;
			ParticleComp->SetVectorParameter(BaseParamName.empty() ? FString("RibbonBase") : BaseParamName, BonePos);
			ParticleComp->SetVectorParameter(TipParamName.empty() ? FString("RibbonTip") : TipParamName, SecondPos);
			// 양손 중앙에서 본 회전 또는 오너 전방 방향을 맞춘다
			if (bFollowBoneRotation)
				ParticleComp->SetWorldLocationAndRotation(MidPos, ExtractRotationFromMatrix(BoneWorld), false);
			else if (Mesh->GetOwner())
				ParticleComp->SetWorldLocationAndRotation(MidPos, Mesh->GetOwner()->GetActorRotation(), false);
			else
				ParticleComp->SetWorldLocation(MidPos, false);
			return;
		}
		if (bFollowBoneRotation)
		{
			ParticleComp->SetWorldLocationAndRotation(BonePos, ExtractRotationFromMatrix(BoneWorld), false);
		}
		else if (Mesh->GetOwner())
		{
			// 전방 방향은 캐릭터 회전을 유지하고 Roll만 소켓에서 가져온다
			FRotator TargetRot = Mesh->GetOwner()->GetActorRotation();
			TargetRot.Roll = ExtractRotationFromMatrix(BoneWorld).Roll;
			ParticleComp->SetWorldLocationAndRotation(BonePos, TargetRot, false);
		}
		else
		{
			FRotator TargetRot = ExtractRotationFromMatrix(Mesh->GetWorldMatrix());
			TargetRot.Roll = ExtractRotationFromMatrix(BoneWorld).Roll;
			ParticleComp->SetWorldLocationAndRotation(BonePos, TargetRot, false);
		}
	}
}

void UAnimNotifyState_ParticleEffect::NotifyBegin(const FAnimNotifyContext& Context)
{
	if (ParticlePath.empty()) return;
	UParticleSystemComponent* ParticleComp = FindTargetParticleComponent(Context.MeshComponent, ParticlePath, true, true);
	if (!ParticleComp)
		return;

	if (!ParticlePath.empty() && ParticleComp->TemplatePath != ParticlePath)
	{
		UParticleSystem* Loaded = UParticleSystem::LoadFromFile(ParticlePath, ParticleComp);
		FString ResolvedPath = ParticlePath;
		if (!Loaded && ParticlePath.rfind("Assets/", 0) != 0)
		{
			ResolvedPath = "Assets/" + ParticlePath;
			Loaded = UParticleSystem::LoadFromFile(ResolvedPath, ParticleComp);
		}
		if (!Loaded) return;
		ParticleComp->TemplatePath = ResolvedPath;
		ParticleComp->SetTemplate(Loaded);
	}

	ActiveComponents.Add(Context.MeshComponent, ParticleComp);
	SyncParticleToBone(Context.MeshComponent, ParticleComp, BoneName, SecondBoneName,
		BaseParameterName, TipParameterName, BoneOffset, bFollowBoneRotation);
	ParticleComp->SetForceLooping(bLoopDuringState);
	ParticleComp->SetVisibility(true);
	ParticleComp->Activate(true);
}

void UAnimNotifyState_ParticleEffect::NotifyTick(const FAnimNotifyContext& Context)
{
	UParticleSystemComponent* ParticleComp = FindTrackedParticleComponent(Context.MeshComponent, ActiveComponents, ParticlePath);
	if (!ParticleComp)
		return;

	SyncParticleToBone(Context.MeshComponent, ParticleComp, BoneName, SecondBoneName,
		BaseParameterName, TipParameterName, BoneOffset, bFollowBoneRotation);
	if (bLoopDuringState)
	{
		ParticleComp->SetForceLooping(true);
		if (!ParticleComp->IsActive())
			ParticleComp->Activate(true);
	}
}

void UAnimNotifyState_ParticleEffect::NotifyEnd(const FAnimNotifyContext& Context)
{
	UParticleSystemComponent* ParticleComp = FindTrackedParticleComponent(Context.MeshComponent, ActiveComponents, ParticlePath);
	ActiveComponents.Remove(Context.MeshComponent);
	if (!ParticleComp)
		return;

	if (!SecondBoneName.empty())
	{
		ParticleComp->ClearVectorParameter(BaseParameterName.empty() ? FString("RibbonBase") : BaseParameterName);
		ParticleComp->ClearVectorParameter(TipParameterName.empty() ? FString("RibbonTip") : TipParameterName);
	}
	ParticleComp->SetForceLooping(false);
	ParticleComp->StopEmission(FadeOutDuration);
}

void UAnimNotify_StartDeathDissolve::Notify(const FAnimNotifyContext& Context)
{
	USkeletalMeshComponent* Mesh = Context.MeshComponent;
	if (!Mesh)
		return;

	if (ACharacter* Character = Cast<ACharacter>(Mesh->GetOwner()))
	{
		Character->SetDeathDissolveParams(DissolveDuration, DissolveEdgeColor);
		Character->StartDeathDissolve();
	}
	else
	{
		Mesh->StartPreviewDissolve(DissolveDuration, DissolveEdgeColor);
	}
}
