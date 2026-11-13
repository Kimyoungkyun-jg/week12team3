#pragma once

#include "ActorComponent.h"
#include "ScriptComponent.generated.h"

struct FLuaScriptInstance;

// Lua 스크립트 하나를 액터에 붙이는 컴포넌트 (담당: A). 한 액터에 여러 개 붙일 수 있다.
// 컴포넌트마다 FLuaScriptInstance(= sol::environment)를 따로 가지므로 스크립트끼리 전역 함수가 섞이지 않는다.
// sol.hpp를 퍼뜨리지 않도록 인스턴스는 포인터로 둔다.
UCLASS()
class UScriptComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScriptComponent();
	virtual ~UScriptComponent() override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual void EndPlay() override;

	// Lua의 BeginPlay를 한 번만 부른다. 액터 BeginPlay 도중이면 액터가 모든 컴포넌트를 시작한 뒤 부른다.
	void StartScript();

	// 액터 이벤트를 Lua로 넘긴다. 함수가 없으면 무시된다.
	void CallScript(const char* Name, AActor* Other);
	void CallScript(const char* Name, float Value);

	const FString& GetScriptPath() const { return ScriptPath; }
	void SetScriptPath(const FString& InPath) { ScriptPath = InPath; }

protected:
	virtual void OnUnregister() override;

private:
	void ReleaseInstance();

	// 디테일 패널의 Lua Script 섹션으로 편집한다 (경로 검사 때문에 문자열 칸으로 노출하지 않는다).
	UPROPERTY()
	FString ScriptPath;

	FLuaScriptInstance* Instance = nullptr;
	bool bScriptStarted = false;
};
