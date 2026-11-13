#include "EnginePCH.h"
#include "ScriptComponent.h"

#include "GameFramework/Actor.h"
#include "Scripting/LuaScriptInstance.h"

UScriptComponent::UScriptComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UScriptComponent::~UScriptComponent()
{
	delete Instance;
	Instance = nullptr;
}

void UScriptComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ScriptPath.empty() || Instance) return;

	// 로드에 실패해도 인스턴스는 남긴다. PIE 중 파일을 고치면 핫 리로드가 다시 읽는다 (기존 AActor 동작과 같음).
	Instance = new FLuaScriptInstance();
	Instance->Load(GetOwner(), ScriptPath);

	// 액터 BeginPlay 중이면 다른 컴포넌트가 모두 시작된 뒤 액터가 StartScript를 부른다.
	// 플레이 중에 새로 붙은 컴포넌트는 여기서 바로 시작한다.
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && !OwnerActor->IsBeginningPlay())
		StartScript();
}

void UScriptComponent::StartScript()
{
	if (!Instance || bScriptStarted) return;
	bScriptStarted = true;
	Instance->Call("BeginPlay");
}

void UScriptComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	AActor* OwnerActor = GetOwner();
	if (Instance && bScriptStarted && OwnerActor && !OwnerActor->IsActorBeingDestroyed())
		Instance->Tick(DeltaTime);
}

void UScriptComponent::CallScript(const char* Name, AActor* Other)
{
	if (Instance && bScriptStarted)
		Instance->Call(Name, Other);
}

void UScriptComponent::CallScript(const char* Name, float Value)
{
	AActor* OwnerActor = GetOwner();
	if (Instance && bScriptStarted && OwnerActor && !OwnerActor->IsActorBeingDestroyed())
		Instance->Call(Name, Value);
}

void UScriptComponent::EndPlay()
{
	if (Instance && bScriptStarted)
		Instance->Call("EndPlay");
	ReleaseInstance();
	Super::EndPlay();
}

void UScriptComponent::OnUnregister()
{
	// 플레이 중에 컴포넌트만 지워지는 경우. 액터 EndPlay를 거치지 않으므로 여기서 정리한다.
	if (Instance)
		EndPlay();
	Super::OnUnregister();
}

void UScriptComponent::ReleaseInstance()
{
	delete Instance;
	Instance = nullptr;
	bScriptStarted = false;
}
