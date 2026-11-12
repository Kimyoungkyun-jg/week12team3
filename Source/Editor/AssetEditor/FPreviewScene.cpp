#include "EnginePCH.h"
#include "FPreviewScene.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Engine/World.h"

#include "Runtime/Components/DirectionalLightComponent.h"
#include "Runtime/Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Runtime/GameFramework/Actor.h"
#include "Runtime/Components/ActorComponent.h"

bool FPreviewScene::Initialize(UEditorEngine* InEditorEngine)
{
	if (bInitialized)
	{
		return true;
	}

	if (!InEditorEngine)
	{
		return false;
	}

	EditorEngine = InEditorEngine;

	// Create World Instance
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, true);
	if (!World)
	{
		return false;
	}

	// Create World Context
	WorldContext = &EditorEngine->CreateNewWorldContext(EWorldType::EditorPreview);
	WorldContext->SetCurrentWorld(World);

	bInitialized = true;
	
	return true;
}

UWorld* FPreviewScene::GetWorld() const
{
	return WorldContext ? WorldContext->World() : nullptr;
}

void FPreviewScene::Tick(float DeltaTime)
{
	if (UWorld* World = GetWorld())
	{
		World->Tick(DeltaTime);
	}
}

void FPreviewScene::Release()
{
	// 삭제는 초기화의 역순

	// Destroy World Context & Instance
	if (WorldContext)
	{
		UWorld* World = WorldContext->World();
		EditorEngine->DestroyWorldContext(World);
		WorldContext = nullptr;
	}

	bInitialized = false;
	bEnvironmentCreated = false;
	EditorEngine = nullptr;
}

AActor* FPreviewScene::AddActor(UClass* ActorClass)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	UWorld* World = WorldContext->World();
	if (!World)
	{
		return nullptr;
	}

	AActor* Actor = World->SpawnActor(ActorClass);
	if (!Actor)
	{
		return nullptr;
	}

	return Actor;
}

UActorComponent* FPreviewScene::AddComponent(UClass* ComponentClass)
{
	AActor* Actor = AddActor(AActor::StaticClass());
	if (!Actor)
	{
		return nullptr;
	}

	// 충돌 기본값이 true이므로, false로 끄기
	Actor->SetActorEnableCollision(false);

	UActorComponent* Component = Actor->AddComponent(ComponentClass, "Preview Component");
	if (!Component)
	{
		GetWorld()->DestroyActor(Actor);
		return nullptr;
	}

	return Component;
}

void FPreviewScene::Setup()
{
	if (!WorldContext || bEnvironmentCreated)
	{
		return;
	}

	UWorld* World = WorldContext->World();
	if (!World)
	{
		return;
	}

	// TODO: SkyPanorama 배경 생성
	// 지금은 관련 기능이 없어서 일단 나중에

	// Directional Light, SkyLight를 붙일 액터 생성
	AActor* SystemActor = World->SpawnActor<AActor>("PreviewSceneManagerActor");
	if (!SystemActor)
	{
		return;
	}
	bEnvironmentCreated = true;
	SystemActor->SetActorEnableCollision(false);

	// Setup Directional Light
	{
		auto _DirectionalLightComponent = SystemActor->AddComponent(
			UDirectionalLightComponent::StaticClass(),
			"DirectionalLightComponent"
		);

		auto DirectionalLightComponent = Cast<UDirectionalLightComponent>(_DirectionalLightComponent);

		if (DirectionalLightComponent)
		{
			DirectionalLightComponent->SetIntensity(0.5f);
			DirectionalLightComponent->SetRelativeRotation(FRotator{40.0f, 230.0f, 0.0f});
		}
	}


	// Setup SkyLight
	{
		auto _SkyLightComponent = SystemActor->AddComponent(
			USkyLightComponent::StaticClass(),
			"SkyLightComponent"
		);

		auto SkyLightComponent = Cast<USkyLightComponent>(_SkyLightComponent);
		if (SkyLightComponent)
		{
			SkyLightComponent->SetIntensity(0.25f);
		}
	}


}
