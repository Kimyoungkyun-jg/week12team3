#pragma once

#include "UObject/Object.h"
#include "Components/PrimitiveComponent.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Containers/Set.h"
#include "Engine/EngineBaseTypes.h"
#include "Actor.generated.h"

class UWorld;
class ULevel;
struct FMinimalViewInfo;

UCLASS()
class AActor : public UObject
{
	GENERATED_BODY()
public:
	AActor();
	virtual ~AActor();

	virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	bool HasBegunPlay() const { return bHasBegunPlay; }
	// 액터 자신의 로직. 컴포넌트는 각자의 PrimaryComponentTick으로 따로 실행된다.
	virtual void Tick(float DeltaTime) {}
	virtual void EndPlay();
	// FActorTickFunction이 호출하는 진입점
	void TickActor(float DeltaTime);

	//Actor 위치의 시점 계산
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV);

	UWorld* GetWorld() const { return World; }
	ULevel* GetLevel() const { return Level; }

	const TArray<UActorComponent*>& GetComponents() const { return Components; }
	USceneComponent* GetRootComponent() const { return RootComponent; }
	// 루트는 이 액터가 소유한 씬 컴포넌트만 지정할 수 있다. nullptr도 허용한다.
	bool SetRootComponent(USceneComponent* SceneComponent);
	// 기존 계층과 월드 Transform을 유지하며 소유한 씬 컴포넌트를 새 루트로 승격한다.
	bool ReplaceRootComponent(USceneComponent* NewRoot);

	// 액터끼리 붙이기 (TEAM5). 이 액터의 루트를 부모 액터의 루트(또는 ParentComponent)에 붙인다. 순환이면 false
	bool AttachToActor(AActor* ParentActor, EAttachmentRule Rule = EAttachmentRule::KeepWorld, USceneComponent* ParentComponent = nullptr);
	void DetachFromActor(EAttachmentRule Rule = EAttachmentRule::KeepWorld);
	AActor* GetAttachParentActor() const;
	// 이 액터의 컴포넌트에 루트가 붙은 다른 액터들 (아웃라이너 트리)
	void GetAttachedActors(TArray<AActor*>& OutActors) const;
	void SetActorEnableCollision(bool bEnabled) { bActorEnableCollision = bEnabled; }
	bool GetActorEnableCollision() const { return bActorEnableCollision; }

	void RemoveOwnedComponent(UActorComponent* Component);
	
	FVector GetActorLocation() const;
	FRotator GetActorRotation() const;
	FVector GetActorScale3D() const;
	FQuat GetActorQuat() const;           //xx 타입명변경
	FTransform GetActorTransform() const;
	bool TryGetActorBounds(FBox& OutBounds) const;

	bool Destroy();
	bool IsActorBeingDestroyed() const { return bActorIsBeingDestroyed; }
	void EndPlayActor();
	
	//xx삭제 예정
	friend class UWorld;

	// 자동 부착 시 첫 씬 컴포넌트가 루트가 된다. 로드는 저장된 계층을 직접 복원한다.
	UActorComponent* AddComponent(UClass* ComponentClass, FName Name, bool bManualAttachment = false);
	void DestroyComponent(UActorComponent* Component);

	// bCanEverTick이 켜진 액터·컴포넌트의 Tick 함수만 World의 FTickTaskManager에 등록하거나 해제한다.
	void RegisterAllActorTickFunctions(bool bRegister);

	// UE와 같이 기본값은 bCanEverTick = false. 생성자에서 Target = this
	FActorTickFunction PrimaryActorTick;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	UActorComponent* FindComponentByName(FName Name) const;

	virtual void RegisterAllComponents();
	// Parameter : 바로 다시 Register 할껀지
	virtual void UnregisterAllComponents();

	// Lua 스크립트는 UScriptComponent로 붙인다 (담당: A). 액터 BeginPlay 도중이면 true
	bool IsBeginningPlay() const { return bIsBeginningPlay; }

	// 컴포넌트 충돌 이벤트를 액터 단위로 받는 지점 (C가 호출, A가 붙은 UScriptComponent 전부에 넘김)
	virtual void NotifyActorBeginOverlap(AActor* OtherActor);
	virtual void NotifyActorEndOverlap(AActor* OtherActor);
	virtual void NotifyHit(AActor* OtherActor);
	virtual void NotifyDeath(AActor* OtherActor);

	// 담당: C
	bool IsOverlappingActor(const AActor* Other) const;
	void GetOverlappingActors(TArray<AActor*>& OutActors) const;
protected:
	virtual void OnDefaultSubobjectCreated(UObject* Subobject) override;

	//TSet<TObjectPtr<UActorComponent>> OwnedComponents;
	TArray<UActorComponent*> Components;
	USceneComponent* RootComponent = nullptr;

	UWorld* World = nullptr;
	ULevel* Level = nullptr;
private:
	UPROPERTY(EditAnywhere)
	bool bActorEnableCollision = true;
	bool bHasBegunPlay = false;
	bool bHasEndedPlay = false;
	bool bIsEndingPlay = false;
	bool bIsBeginningPlay = false;
	bool bActorIsBeingDestroyed = false;

	// 붙은 UScriptComponent 전부에 Lua 함수 호출을 넘긴다. 호출 중 컴포넌트가 바뀌어도 안전하게 복사본을 돈다.
	void CallScripts(const char* Name, AActor* OtherActor);

};
