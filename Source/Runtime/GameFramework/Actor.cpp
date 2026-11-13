#include "EnginePCH.h"
#include "Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/SceneComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Core/EngineLog.h"
#include "UObject/Object.h"

#include "Components/ScriptComponent.h"
#include "Scripting/LuaBindings.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
enum class EAttachmentRule;


AActor::AActor()
{
	PrimaryActorTick.Target = this;
}

AActor::~AActor()
{
	UnregisterAllComponents();

	TArray<UActorComponent*> ToDelete = Components;
	Components.Reset();
	RootComponent = nullptr;

	for (UActorComponent* Component : ToDelete)
	{
		delete Component;
	}
}

void AActor::BeginPlay()
{
	if (bActorIsBeingDestroyed || bHasBegunPlay || bHasEndedPlay || !World || !World->HasBegunPlay()) return;
	RegisterAllComponents();
	if (bActorIsBeingDestroyed) return;
	bHasBegunPlay = true;

	// UScriptComponent는 BeginPlay에서 스크립트를 로드만 하고, Lua BeginPlay는 아래에서
	// 모든 컴포넌트가 시작된 뒤에 부른다 (스크립트가 다른 컴포넌트를 바로 쓸 수 있게).
	bIsBeginningPlay = true;
	const TArray<UActorComponent*> BeginningComponents = Components;
	for (UActorComponent* Component : BeginningComponents)
	{
		if (bActorIsBeingDestroyed) break;
		if (Components.Find(Component) != INDEX_NONE && IsValid(Component)) Component->BeginPlayComponent();
	}
	bIsBeginningPlay = false;
	if (bActorIsBeingDestroyed) return;

	RegisterAllActorTickFunctions(true);

	const TArray<UActorComponent*> ScriptCandidates = Components;
	for (UActorComponent* Component : ScriptCandidates)
	{
		if (bActorIsBeingDestroyed) return;
		if (Components.Find(Component) == INDEX_NONE || !IsValid(Component)) continue;
		if (UScriptComponent* ScriptComponent = Cast<UScriptComponent>(Component))
			ScriptComponent->StartScript();
	}
}

void AActor::TickActor(float DeltaTime)
{
	if (bActorIsBeingDestroyed) return;
	Tick(DeltaTime);
}

void AActor::EndPlayActor()
{
	if (!bHasBegunPlay || bHasEndedPlay || bIsEndingPlay) return;
	bHasEndedPlay = true;
	bHasBegunPlay = false;
	bIsEndingPlay = true;
	EndPlay();
	bIsEndingPlay = false;
}

void AActor::EndPlay()
{
	// 컴포넌트 EndPlay (UScriptComponent는 여기서 Lua EndPlay를 부르고 인스턴스를 해제한다)
	const TArray<UActorComponent*> EndingComponents = Components;
	for (UActorComponent* Component : EndingComponents)
	{
		if (Components.Find(Component) != INDEX_NONE && IsValid(Component))
			Component->EndPlay();
	}
	FCoroutineScheduler::Get().StopAll(this);   // B의 코루틴이 지워질 액터를 다시 깨우지 않게
	FLuaBindings::ReleaseCallbacks(this);       // 델리게이트에 걸린 Lua 함수 해제 (단계 8 안전장치 참고)
}

void AActor::CallScripts(const char* Name, AActor* OtherActor)
{
	const TArray<UActorComponent*> Snapshot = Components;
	for (UActorComponent* Component : Snapshot)
	{
		if (bActorIsBeingDestroyed) return;
		if (Components.Find(Component) == INDEX_NONE || !IsValid(Component)) continue;
		if (UScriptComponent* ScriptComponent = Cast<UScriptComponent>(Component))
			ScriptComponent->CallScript(Name, OtherActor);
	}
}

void AActor::RegisterAllActorTickFunctions(bool bRegister)
{
	if (bRegister && (!World || bActorIsBeingDestroyed))
		return;

	// bCanEverTick이 꺼진 함수는 등록하지 않으므로 정적 메시 액터는 매 프레임 순회 대상에서 빠진다.
	auto Apply = [&](FTickFunction& Function)
		{
			if (bRegister)
				Function.RegisterTickFunction(World->GetTickTaskManager());
			else
				Function.UnRegisterTickFunction();
		};

	Apply(PrimaryActorTick);
	for (UActorComponent* Component : Components)
	{
		if (Component)
			Component->RegisterComponentTickFunctions(bRegister);
	}
}

void AActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Components.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Components", Num);
		if (Ar.IsLoading())
			Components.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Components[i];
			ActorArray.EnterElement() << Obj;
			Components[i] = static_cast<UActorComponent*>(Obj);
		}

		UObject* WorldObj = World;
		UObject* LevelObj = Level;
		UObject* RootObj = RootComponent;
		Record << SA_VALUE("OwningWorld", WorldObj);
		Record << SA_VALUE("OwningLevel", LevelObj);
		Record << SA_VALUE("RootComponent", RootObj);
		World = static_cast<UWorld*>(WorldObj);
		Level = static_cast<ULevel*>(LevelObj);
		RootComponent = static_cast<USceneComponent*>(RootObj);
		return;
	}

	FString RootName = RootComponent ? RootComponent->GetName() : FString();
	bool bHasSavedRoot = false;
	if (auto RootSlot = Record.TryEnterField("RootComponentName", true))
	{
		*RootSlot << RootName;
		bHasSavedRoot = true;
	}

	TArray<UActorComponent*> SavedComponents;
	if (Ar.IsSaving())
	{
		for (UActorComponent* Component : Components)
		{
			if (Component)
			{
				SavedComponents.Add(Component);
			}

		}
	}

	struct FPendingAttachment
	{
		USceneComponent* Child;
		FString ParentName;
	};
	TArray<FPendingAttachment> PendingAttachments;

	int32 Num = SavedComponents.Num();
	FStructuredArchive::FArray ComponentArray = Record.EnterArray("Components", Num);
	for (int32 i = 0; i < Num; ++i)
	{
		FStructuredArchive::FRecord ComponentRecord = ComponentArray.EnterElement().EnterRecord();
		FString ParentName;

		if (Ar.IsSaving())
		{
			UActorComponent* Component = SavedComponents[i];
			FString Name = Component->GetName();
			FString ClassName = Component->GetClass()->Name;
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				ParentName = SceneComponent->GetAttachParent() ? SceneComponent->GetAttachParent()->GetName() : "";
			}
			ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName);
			Component->Serialize(ComponentRecord);
			continue;
		}

		FString Name, ClassName;
		ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName);

		UActorComponent* Component = FindComponentByName(FName(Name));
		if (!Component)
		{
			UClass* ComponentClass = FindClass(ClassName);

			if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
			{
				HTR_LOG(Warning, "Load: unknown component class '{}'", ClassName);
				continue;
			}

			Component = AddComponent(ComponentClass, FName(Name), true);
			// 생성자에 없던 컴포넌트는 사용자가 추가한 것이다 (이름 변경 허용)
			if (Component)
				Component->MarkUserCreated();
		}

		if (!Component || Component->GetClass()->Name != ClassName)
		{
			HTR_LOG(Warning, "Load: component {} ({}) failed", Name, ClassName);
			continue;
		}

		// 생성자와 AddComponent의 임시 계층을 풀고 저장된 계층을 나중에 복원한다.
		// 루트가 바뀐 경우 생성자의 연결이 남아 있으면 순환 검사에 걸릴 수 있다.
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
			SceneComponent)
		{
			SceneComponent->DetachFromParent(EAttachmentRule::KeepRelative);
		}

		Component->Serialize(ComponentRecord);
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			if (!ParentName.empty())
			{
				PendingAttachments.Add({ SceneComponent, ParentName });
			}
		}
	}

	for (const FPendingAttachment& Attachment : PendingAttachments)
	{
		USceneComponent* ParentComponent = Cast<USceneComponent>(FindComponentByName(FName(Attachment.ParentName)));
		if (ParentComponent)
		{
			Attachment.Child->SetupAttachment(ParentComponent);
		}
	}

	if (Ar.IsLoading())
	{
		if (bHasSavedRoot)
		{
			// 빈 이름은 루트 없는 네이티브 액터를 뜻한다.
			SetRootComponent(Cast<USceneComponent>(FindComponentByName(FName(RootName))));
		}
		else
		{
			// 이전 씬에는 루트 이름이 없다. 네이티브 루트의 저장된 부모를 따라가거나,
			// 이전 AActor 생성자가 만들던 DefaultSceneRoot를 사용한다.
			USceneComponent* SavedRoot = RootComponent;
			if (!SavedRoot)
				SavedRoot = Cast<USceneComponent>(FindComponentByName("DefaultSceneRoot"));
			while (SavedRoot && SavedRoot->GetAttachParent() && SavedRoot->GetAttachParent()->GetOwner() == this)
				SavedRoot = SavedRoot->GetAttachParent();
			SetRootComponent(SavedRoot);
		}
	}
}


bool AActor::SetRootComponent(USceneComponent* SceneComponent)
{
	if (SceneComponent && SceneComponent->GetOwner() != this) return false;
	RootComponent = SceneComponent;
	return true;
}

bool AActor::ReplaceRootComponent(USceneComponent* NewRoot)
{
	if (!IsValid(NewRoot) || NewRoot->GetOwner() != this ||
		Components.Find(NewRoot) == INDEX_NONE || NewRoot->IsHiddenInDetails() || bActorIsBeingDestroyed)
		return false;
	if (NewRoot == RootComponent) return true;

	USceneComponent* OldRoot = RootComponent;
	const bool bRemoveDefaultRoot = World && OldRoot && OldRoot->IsUserCreated() &&
		OldRoot->GetClass() == USceneComponent::StaticClass() && OldRoot->GetFName() == FName("DefaultSceneRoot");
	USceneComponent* ExternalParent = OldRoot ? OldRoot->GetAttachParent() : nullptr;
	NewRoot->DetachFromParent(EAttachmentRule::KeepWorld);
	if (OldRoot) OldRoot->DetachFromParent(EAttachmentRule::KeepWorld);
	RootComponent = NewRoot;
	NewRoot->SetupAttachment(ExternalParent, EAttachmentRule::KeepWorld);
	if (OldRoot) OldRoot->SetupAttachment(NewRoot, EAttachmentRule::KeepWorld);
	// 에디터가 만든 대체용 루트는 실제 컴포넌트로 교체하면 제거한다.
	// 네이티브 액터가 직접 만든 루트는 일반 컴포넌트로 유지한다.
	if (bRemoveDefaultRoot) DestroyComponent(OldRoot);
	return true;
}

bool AActor::AttachToActor(AActor* ParentActor, EAttachmentRule Rule, USceneComponent* ParentComponent)
{
	if (!RootComponent || !ParentActor || ParentActor == this)
		return false;
	USceneComponent* Target = ParentComponent ? ParentComponent : ParentActor->GetRootComponent();
	if (!Target)
		return false;
	// SetupAttachment는 순환이면 아무것도 하지 않으므로 결과로 성공 여부를 판단한다.
	RootComponent->SetupAttachment(Target, Rule);
	return RootComponent->GetAttachParent() == Target;
}

void AActor::DetachFromActor(EAttachmentRule Rule)
{
	if (RootComponent && GetAttachParentActor())
		RootComponent->DetachFromParent(Rule);
}

AActor* AActor::GetAttachParentActor() const
{
	USceneComponent* Parent = RootComponent ? RootComponent->GetAttachParent() : nullptr;
	AActor* Owner = Parent ? Parent->GetOwner() : nullptr;
	return Owner != this ? Owner : nullptr;
}

void AActor::GetAttachedActors(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	for (UActorComponent* Component : Components)
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (!SceneComponent)
			continue;
		for (USceneComponent* Child : SceneComponent->GetAttachChildren())
		{
			AActor* ChildOwner = Child ? Child->GetOwner() : nullptr;
			if (ChildOwner && ChildOwner != this && Child == ChildOwner->GetRootComponent() && OutActors.Find(ChildOwner) == INDEX_NONE)
				OutActors.Add(ChildOwner);
		}
	}
}

void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
	for (uint32 i = 0; i < Components.Num(); ++i)
	{
		if (Components[i] == Component)
		{
			Components.RemoveAt(i, 1);
			break;
		}
	}

	if (RootComponent == Component)
	{
		RootComponent = nullptr;
	}
}

FVector AActor::GetActorLocation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldLocation();
	}
	return FVector::ZeroVector;
}

FRotator AActor::GetActorRotation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation();
	}
	return FRotator();
}

FVector AActor::GetActorScale3D() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldScale3D();
	}
	return FVector::OneVector;
}

FQuat AActor::GetActorQuat() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation().Quaternion();
	}
	return FQuat::Identity();
}

FTransform AActor::GetActorTransform() const
{
	if (RootComponent)
	{
		return FTransform(
			RootComponent->GetWorldRotation(),
			RootComponent->GetWorldLocation(),
			RootComponent->GetWorldScale3D()
		);

		// return FTransform(RootComponent->GetWorldMatrix());
	}
	return FTransform::Identity;
}

bool AActor::Destroy()
{
	if (bActorIsBeingDestroyed || IsGarbage()) return true;
	if (!World)
		return false;

	return World->DestroyActor(this);
}

UActorComponent* AActor::AddComponent(UClass* ComponentClass, FName Name, bool bManualAttachment)
{
	if (bActorIsBeingDestroyed || !ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return nullptr;
	}


	if (FName(Name) == FName("None"))
	{
		Name = MakeUniqueObjectName(this, ComponentClass);
	}

	UActorComponent* NewComponent = NewObject<UActorComponent>(this, ComponentClass, Name);
	if (!NewComponent) { return nullptr; }

	NewComponent->SetOwner(this);
	Components.Add(NewComponent);
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(NewComponent); SceneComponent && !bManualAttachment)
	{
		if (!RootComponent)
			SetRootComponent(SceneComponent);
		else
			SceneComponent->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
	}

	NewComponent->RegisterComponent();

	return NewComponent;
}

void AActor::DestroyComponent(UActorComponent* Component)
{
	if (!IsValid(Component) || Components.Find(Component) == INDEX_NONE)
		return;

	if (Component == RootComponent)
	{
		HTR_LOG(Warning, "The root component cannot be deleted. Replace it first.");
		return;
	}

	if (USceneComponent* Parent = Cast<USceneComponent>(Component))
	{
		TArray<USceneComponent*> Children = Parent->GetAttachChildren();
		for (USceneComponent* Candidate : Children)
		{
			if (!Candidate) { continue; }

			Candidate->DetachFromParent(EAttachmentRule::KeepWorld);
			Candidate->SetupAttachment(Parent->GetAttachParent(), EAttachmentRule::KeepWorld);
		}
	}

	UWorld* OwningWorld = World;
	if (!OwningWorld) return;
	Component->MarkAsGarbage();
	Component->UnregisterComponent();
	RemoveOwnedComponent(Component);
	Component->SetOwner(nullptr);
	OwningWorld->QueueObjectDeletion(Component);
	return;
}

void AActor::OnDefaultSubobjectCreated(UObject* Subobject)
{
	if (UActorComponent* Component = Cast<UActorComponent>(Subobject))
	{
		Component->SetOwner(this);
		Components.Add(Component);
	}
}

UActorComponent* AActor::FindComponentByName(FName Name) const
{
	for (UActorComponent* Component : Components)
		if (Component && Component->GetFName() == Name)
			return Component;
	return nullptr;
}

void AActor::RegisterAllComponents()
{
	if (!World) return;

	for (UActorComponent* Component : Components)
	{
		if (Component && !Component->IsRegistered())
			Component->RegisterComponentWithWorld(World);
	}
}

void AActor::UnregisterAllComponents()
{
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->IsRegistered())
			Component->UnregisterComponent();
	}
}

bool AActor::TryGetActorBounds(FBox& OutBounds) const
{
	bool bHasBounds = false;

	for (UActorComponent* Component : Components)
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
			continue;

		FBox PrimitiveBounds = Primitive->CalcBounds();
		if (bHasBounds)
		{
			OutBounds.Expand(PrimitiveBounds);
		}
		else
		{
			OutBounds = PrimitiveBounds;
			bHasBounds = true;
		}
	}

	return bHasBounds;
}

void AActor::NotifyActorBeginOverlap(AActor* OtherActor)
{
	if (bActorIsBeingDestroyed) return;
	CallScripts("OnBeginOverlap", OtherActor);
}

void AActor::NotifyActorEndOverlap(AActor* OtherActor)
{
	if (bActorIsBeingDestroyed) return;
	CallScripts("OnEndOverlap", OtherActor);
}

void AActor::NotifyHit(AActor* OtherActor)
{
	if (bActorIsBeingDestroyed) return;
	CallScripts("OnHit", OtherActor);
}

void AActor::NotifyDeath(AActor* OtherActor)
{
	if (bActorIsBeingDestroyed) return;
	CallScripts("OnDeath", OtherActor);
}

bool AActor::IsOverlappingActor(const AActor* Other) const
{
	if (!Other)
		return false;

	for (UActorComponent* Component : Components)
	{
		UPrimitiveComponent* Primitive =
			Cast<UPrimitiveComponent>(Component);

		if (!Primitive)
			continue;

		for (const FOverlapInfo& Info :
			Primitive->GetOverlapInfos())
		{
			if (Info.OtherComp &&
				Info.OtherComp->GetOwner() == Other)
			{
				return true;
			}
		}
	}

	return false;	return false;
}

void AActor::GetOverlappingActors(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	TSet<AActor*> Seen;
	for (UActorComponent* Component : Components)
	{
		const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive || !Primitive->IsRegistered()) continue;
		for (const FOverlapInfo& Info : Primitive->GetOverlapInfos())
		{
			if (!Info.OtherComp || !Info.OtherComp->IsRegistered()) continue;
			AActor* Other = Info.OtherComp->GetOwner();
			if (Other && Other != this && Other->GetWorld() == GetWorld() && Seen.Add(Other))
				OutActors.Add(Other);
		}
	}
}

void AActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV)
{

	// 카메라있음 -> 정보 전달
	for (UActorComponent* Component : GetComponents())
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
		{
			Camera->GetCameraView(DeltaTime, OutPOV);
			return;
		}
	}
	// 카메라없음 -> 액터자신
	OutPOV.Location = GetActorLocation();
	OutPOV.Rotation = GetActorRotation();
}
