#include "EnginePCH.h"
#include "World.h"
#include "Level.h"

#include "Core/EngineStatics.h"
#include "GameFramework/Actor/StaticMeshActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameHUD.h"
#include "UObject/UObjectHash.h"
#include "GameFramework/SpringArmComponent.h"
#include "Input/InputSystem.h"

#include "UObject/UObjectIterator.h"

#include "Collision/Ray.h"
#include "Components/BillboardComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

#include "Math/Frustum.h"
#include "Math/BVH.h"

#include <algorithm>
#include <span>
#include <unordered_map>
#include <vector>

#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/EditorStats.h"
#include "Core/Async/TaskPool.h"
#include "Render/ObjectConstants.h"

#include "UObject/UObjectGlobals.h"

#include "Scripting/LuaBindings.h"
#include "Physics/Collision.h"
#include "Components/ShapeComponent.h"
#include "Components/CapsuleComponent.h"

DECLARE_CYCLE_STAT("Actor Tick", STAT_ActorTick); // Actor 틱 측정
DECLARE_CYCLE_STAT("Update All Transforms", STAT_UpdateAllTransforms); // 각 Transform의 Update 시간 측정

UWorld::~UWorld()
{
	FlushPendingDestruction();
}

bool UWorld::Init()
{
	// Spawn Actor로 카메라 생성하고 세팅하기
	PersistentLevel = NewObject<ULevel>(this);

	if (!PersistentLevel)
	{
		HTR_LOG(Error, "Failed to create PersistentLevel");
		return false;
	}

	//레벨 연결
	PersistentLevel->SetWorld(this);
	Levels.Add(PersistentLevel);
	CurrentLevel = PersistentLevel;

	//카메라 생성
	CreateMainCamera();

	return true;
}

AActor* UWorld::SpawnActor(UClass* Class, FName InName, const FTransform* Transform)
{
	if (bIsClearingWorld || !Class) return nullptr;
	if (!Class->IsChildOf(AActor::StaticClass())) return nullptr;

	// 1. ObjectFactory로 Actor 생성
	AActor* NewActor = NewObject<AActor>(PersistentLevel, Class, InName);

	if (!NewActor)
	{
		HTR_LOG(Error, "SpawnActor : Failed to create Actor");
		return nullptr;
	}

	// 2. Actor에 World/Level 연결
	NewActor->World = this;
	NewActor->Level = PersistentLevel;

	// 3. Transform 적용
	const FTransform SpawnTransform = Transform ? *Transform : FTransform::Identity;

	if (NewActor->GetRootComponent())
	{
		NewActor->GetRootComponent()->SetTransform(SpawnTransform);
	}
	
	// 4. Level->Actors에 등록한 뒤 컴포넌트를 월드에 연결한다.
	PersistentLevel->AddActor(NewActor);
	NewActor->RegisterAllComponents();
	if (!IsValid(NewActor) || NewActor->IsActorBeingDestroyed()) return nullptr;

	// 월드가 재생 중일 때 생성된 액터만 별도 BeginPlay 대기열에 넣는다.
	if (bBegunPlay) BeginPlayList.Enqueue(NewActor);

	return NewActor;
}

void UWorld::Tick(float RealDeltaTime)
{
	if (bIsTicking || bIsClearingWorld || ActiveObjectCalls) return;
	bIsTicking = true;
	RealTimeSeconds += RealDeltaTime;

	if (HitStopTimeRemaining > 0.0f)
	{
		HitStopTimeRemaining = std::max(HitStopTimeRemaining - RealDeltaTime, 0.0f);
	}

	const float DeltaTime = RealDeltaTime * GetEffectiveTimeDilation();

	if (!bIsPaused)
	{
		TimeSeconds += DeltaTime;
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}

	// 에디터 월드가 아니면 액터 틱을 돌린다
	if (WorldType != EWorldType::Editor)
	{
		while (!BeginPlayList.IsEmpty())
		{
			AActor* Actor = BeginPlayList.Peek();
			BeginPlayList.Dequeue();
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->BeginPlay();
		}

		if (!bIsPaused)
		{
			SCOPE_CYCLE_COUNTER(STAT_ActorTick);
			// 모든 Actor를 도는 대신 등록된 Tick 함수만 실행한다
			TickTaskManager.RunAllTickGroups(DeltaTime);
			FCoroutineScheduler::Get().Tick(DeltaTime);

			UpdateOverlaps();
			FLuaBindings::FlushPendingDestroy(this);
			if (IsValid(PlayerController) && !PlayerController->IsActorBeingDestroyed())
				PlayerController->UpdateCameraManager(RealDeltaTime);
		}
	}
	else
	{
		for (TObjectIterator<UParticleSystemComponent> Comp; Comp; ++Comp)
		{
			if (Comp && Comp->IsRegistered() && Comp->GetWorld() == this && Comp->bPreviewInEditor)
				Comp->TickComponent(DeltaTime);
		}
		// 에디터 월드에서도 파티클 시뮬레이션은 틱이 돌아야 한다
		for (TObjectIterator<UParticleSubUVComponent> Comp; Comp; ++Comp)
		{
			if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
			{
				continue;
			}
			if (Comp->GetParticlesForView().IsEmpty())
			{
				Comp->SetParticles(10);
			}
			Comp->TickComponent(DeltaTime);
		}
		// 스프링 암 컴포넌트도 에디터에서 틱이 돌아야 한다
		for (TObjectIterator<USpringArmComponent> Comp; Comp; ++Comp)
		{
			if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
			{
				continue;
			}
			Comp->UpdateInEditor();
		}
	}
	bIsTicking = false;
}

void UWorld::ClearWorld()
{
	if (bIsClearingWorld) return;
	bIsClearingWorld = true;
	EndPlay();
	// BeginPlay 대기 중인 Actor 제거
	while (!BeginPlayList.IsEmpty())
	{
		BeginPlayList.Dequeue();
	}

	
	for (ULevel* Level : Levels)
	{
		const TArray<AActor*> Actors = Level->Actors;
		for (AActor* Actor : Actors)
		{
			if (!IsValid(Actor)) continue;
			Actor->EndPlayActor();
			Actor->UnregisterAllComponents();
			Actor->RegisterAllActorTickFunctions(false);
		}
		Level->ClearActors();
	}
	FLuaBindings::DiscardPendingDestroy(this);
	// 액터를 지우기 전에 렌더 프록시와 틱 등록부터 푼다. ClearActors는 액터를 delete만 하므로,
	// 그대로 두면 지워진 컴포넌트를 가리키는 프록시가 FScene에 남아 다음 프레임에 터진다.
	Scene.RemoveAllLights();
	Scene.RemoveAllPrimitives();
	HTR_LOG(Info, "{} : ", PersistentLevel->GetActorNum());
	bIsClearingWorld = false;
	FlushPendingDestruction();
}

// 메인 카메라 생성
void UWorld::CreateMainCamera()
{
	if (MainCamera)
		return;

	MainCamera = NewObject<ACameraActor>();

	if (!MainCamera)
	{
		HTR_LOG(Error, "Failed to create MainCamera");
		return;
	}

	MainCamera->World = this;
	MainCamera->Level = nullptr;
	MainCamera->GetCameraComponent()->SetRelativeLocation(FVector(-5.0f, -5.0f, 5.0f));

	// 메인 카메라는 Level에 속하지 않아 BeginPlay를 거치지 않으므로 여기서 등록한다.
	MainCamera->RegisterAllActorTickFunctions(true);
}

void UWorld::SetMainCamera(ACameraActor* Camera)
{
	if (MainCamera == Camera)
		return;

	if (MainCamera)
		MainCamera->RegisterAllActorTickFunctions(false);

	MainCamera = Camera;

	if (MainCamera)
	{
		MainCamera->World = this;
		MainCamera->RegisterAllActorTickFunctions(true);
	}
}

int32 UWorld::GetActorNum()
{
	return PersistentLevel->GetActorNum();
}

bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor)
		return false;
	if (Actor->bActorIsBeingDestroyed || Actor->IsGarbage()) return true;
	if (Actor->bIsEndingPlay || bIsClearingWorld) return false;

	ULevel* Level = Actor->GetLevel();

	if (!Level || Actor->GetWorld() != this || Level->Actors.Find(Actor) == INDEX_NONE)
		return false;
	++ActiveObjectCalls;
	struct FCallGuard { int32& Count; ~FCallGuard() { --Count; } } CallGuard{ ActiveObjectCalls };
	Actor->bActorIsBeingDestroyed = true;
	if (Actor == HUD) { if (PlayerController) PlayerController->SetHUD(nullptr); HUD = nullptr; }
	if (Actor == PlayerController)
	{
		PlayerController->UnPossess();
		PlayerController->SetHUD(nullptr);
		if (HUD) HUD->SetOwningPlayerController(nullptr);
		PlayerController = nullptr;
	}
	if (Actor == PlayerPawn) { if (PlayerController) PlayerController->UnPossess(); PlayerPawn = nullptr; }
	if (Actor == GameMode) GameMode = nullptr;



	// 1. BeginPlay 대기열에서 제거
	TQueue<AActor*> NewBeginPlayList;

	while (!BeginPlayList.IsEmpty())
	{
		AActor* PendingActor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (PendingActor != Actor)
		{
			NewBeginPlayList.Enqueue(PendingActor);
		}
	}

	BeginPlayList = std::move(NewBeginPlayList);

	// 2. Level의 Actors에서 제거
	for (int32 i = Level->Actors.Num() - 1; i >= 0; --i)
	{
		if (Level->Actors[i] == Actor)
		{
			Level->Actors.RemoveAt(i, 1);
			break;
		}
	}

	FString ActorName = Actor->GetName();
	uint32 ActorUUID = Actor->GetUUID();

	Actor->EndPlayActor();

	// 3. 프록시 제거
	Actor->UnregisterAllComponents();

	Actor->RegisterAllActorTickFunctions(false);
	if (APawn* Pawn = Cast<APawn>(Actor))
		if (APlayerController* Controller = Pawn->GetController()) Controller->UnPossess();
	if (APlayerController* Controller = Cast<APlayerController>(Actor)) Controller->UnPossess();

	// 4. Actor 삭제
	// Actor 메모리는 프레임 안전 지점에서 해제
	QueueObjectDeletion(Actor);

	HTR_LOG(Info, "Destroy Actor : {} UUID {}", ActorName, ActorUUID);

	return true;
}

void UWorld::QueueObjectDeletion(UObject* Object)
{
	if (!Object || PendingDeletion.Find(Object) != INDEX_NONE) return;
	Object->MarkAsGarbage();
	PendingDeletion.Add(Object);
}

void UWorld::FlushPendingDestruction()
{
	if (bIsTicking || bIsClearingWorld || ActiveObjectCalls || bFlushingDestruction) return;
	bFlushingDestruction = true;
	while (!PendingDeletion.IsEmpty())
	{
		TArray<UObject*> Objects = std::move(PendingDeletion);
		PendingDeletion.Reset();
		TArray<UObject*> Actors;
		for (UObject* Object : Objects)
		{
			if (Object->IsA<AActor>()) Actors.Add(Object);
			else delete Object;
		}
		for (UObject* Actor : Actors) delete Actor;
	}
	bFlushingDestruction = false;
}

// 다른 World의 객체를 제외하고 Component 교차 중 최근접 결과를 선택한다.
bool UWorld::SweepSingle(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor,
	bool bBlockingOnly)
{
	OutHit = FHitResult(1.0f);
	if (MaxDistance <= 0.0f || !std::isfinite(MaxDistance) || Radius < 0.0f || !std::isfinite(Radius)) return false;
	Scene.UpdateAllTransforms();
	float NearestT = MaxDistance;
	const FVector Extent(Radius, Radius, Radius);
	if (bBlockingOnly)
	{
		// 도형은 렌더 프록시가 없으므로 직접 검사한다. 표시 여부는 충돌 여부와 별개다.
		// 기존 메시 Sweep과 같은 확장 AABB 방식이며, 바닥/트리거는 bBlockComponent=false로 둔다.
		// 전체 오브젝트 풀 대신 현재 월드의 레벨 액터만 순회해 스윕 비용을 줄인다
		for (ULevel* Level : Levels)
		{
			if (!Level) continue;
			for (AActor* Owner : Level->GetActors())
			{
				if (!IsValid(Owner) || Owner == IgnoreActor || !Owner->GetActorEnableCollision()) continue;
				for (UActorComponent* RawComp : Owner->GetComponents())
				{
					UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(RawComp);
					if (!Component || !Component->IsRegistered() || !Component->bBlockComponent) continue;
					if (!Cast<UShapeComponent>(Component) && !Component->GetMeshData()) continue;

					const FBox Bounds = Component->CalcBounds();
					const FVector Min = Bounds.Min - Extent, Max = Bounds.Max + Extent;
					float Enter = 0.0f, Exit = NearestT;
					FVector Normal = -WorldRay.Direction;
					bool bIntersects = true;
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						const float Origin = WorldRay.Origin[Axis], Direction = WorldRay.Direction[Axis];
						if (std::abs(Direction) <= SMALL_NUMBER)
						{
							if (Origin < Min[Axis] || Origin > Max[Axis]) { bIntersects = false; break; }
							continue;
						}
						float Near = (Min[Axis] - Origin) / Direction, Far = (Max[Axis] - Origin) / Direction;
						if (Near > Far) std::swap(Near, Far);
						if (Near > Enter)
						{
							Enter = Near;
							Normal = FVector::ZeroVector;
							Normal[Axis] = Direction > 0.0f ? -1.0f : 1.0f;
						}
						Exit = std::min(Exit, Far);
						if (Enter > Exit) { bIntersects = false; break; }
					}
					if (!bIntersects) continue;
					if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Component))
					{
						if (!FCollision::SweepSphereCapsule(WorldRay, NearestT, Radius,
							FCollision::ToWorld(Capsule), Enter, Normal)) continue;
					}
					NearestT = Enter;
					OutHit.bBlockingHit = true;
					OutHit.bStartPentrating = Enter == 0.0f;
					OutHit.HitComponent = Component;
					OutHit.Distance = Enter;
					OutHit.Time = Enter / MaxDistance;
					OutHit.Location = WorldRay.PointAt(Enter);
					OutHit.ImpactPoint = OutHit.Location - Normal * Radius;
					OutHit.Normal = OutHit.ImpactNormal = Normal;
				}
			}
		}
		return OutHit.bBlockingHit;
	}
	const FPreparedRay PreparedRay(WorldRay);
	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT)
		{
			return RayIntersectsAABB(PreparedRay, Bounds.Min - Extent, Bounds.Max + Extent, OutEnterT);
		},
		[&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			UPrimitiveComponent* Component = Scene.FindPrimitiveComponent(Proxy);
			if (!Component || !Component->IsShown(true) ||
				!Component->GetOwner() || !Component->GetOwner()->GetActorEnableCollision() ||
				Component->GetOwner() == IgnoreActor || Component->GetOwner()->GetWorld() != this)
				return false;
			if (!Proxy->GetMesh())
			{
				if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component))
				{
					float T = InOutNearestT;
					FVector Normal = -WorldRay.Direction;
					if (!SkelComp->SweepHitboxes(WorldRay, InOutNearestT, Radius, T, Normal) || T > InOutNearestT)
						return false;
					OutHit.bBlockingHit = true;
					OutHit.bStartPentrating = (T == 0.0f);
					OutHit.HitComponent = Component;
					OutHit.Distance = T;
					OutHit.Time = T / MaxDistance;
					OutHit.Location = WorldRay.PointAt(T);
					OutHit.ImpactPoint = OutHit.Location - Normal * Radius;
					OutHit.ImpactNormal = Normal;
					OutHit.Normal = Normal;
					InOutNearestT = T;
					return true;
				}
				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};
				float T = InOutNearestT;
				if (!Component->LineTraceComponentLocal(LocalRay, T) || T > InOutNearestT) return false;
				OutHit.bBlockingHit = true;
				OutHit.HitComponent = Component;
				OutHit.Distance = T;
				OutHit.Time = T / MaxDistance;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				OutHit.Location = OutHit.ImpactPoint;
				OutHit.ImpactNormal = -WorldRay.Direction;
				OutHit.Normal = OutHit.ImpactNormal;
				InOutNearestT = T;
				return true;
			}

			FAABB Bounds = Proxy->GetBounds();
			Bounds.Extent += Extent;
			float T = InOutNearestT;
			if (!RayIntersectsAABB(WorldRay, Bounds.Center - Bounds.Extent, Bounds.Center + Bounds.Extent, T) ||
				T > InOutNearestT) return false;

			const FVector Point = WorldRay.Origin + WorldRay.Direction * T;
			const FVector Offset = Point - Bounds.Center;
			const float X = Bounds.Extent.X > 0.0f ? Offset.X / Bounds.Extent.X : 0.0f;
			const float Y = Bounds.Extent.Y > 0.0f ? Offset.Y / Bounds.Extent.Y : 0.0f;
			const float Z = Bounds.Extent.Z > 0.0f ? Offset.Z / Bounds.Extent.Z : 0.0f;
			const float AX = std::abs(X), AY = std::abs(Y), AZ = std::abs(Z);
			FVector Normal = AX >= AY && AX >= AZ ? FVector(X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f)
				: AY >= AZ ? FVector(0.0f, Y >= 0.0f ? 1.0f : -1.0f, 0.0f)
				: FVector(0.0f, 0.0f, Z >= 0.0f ? 1.0f : -1.0f);
			if (Normal.Dot(WorldRay.Direction) > 0.0f) return false;

			OutHit.bBlockingHit = true;
			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.Time = T / MaxDistance;
			OutHit.ImpactPoint = Point;
			OutHit.Location = Point;
			OutHit.ImpactNormal = Normal;
			OutHit.Normal = Normal;
			InOutNearestT = T;
			return true;
		}, NearestT);
	return OutHit.bBlockingHit;
}

bool UWorld::LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
	FBillboardTraceTransform ResolveBillboard, const void* ViewContext)
{
	SCOPE_CYCLE_COUNTER_ALWAYS(EditorStats::STAT_PickingTime_Name);
	OutHit = FHitResult();
	float NearestT = std::numeric_limits<float>::max();

	const auto TraceComponent = [&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			if (UStaticMesh* Mesh = Proxy ? Proxy->GetMesh() : nullptr)
			{
				if (!Proxy->IsVisible())
					return false;

				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};

				float T = InOutNearestT;
				if (!RayIntersectsMesh(LocalRay, Mesh->GetMeshData(), T))
					return false;

				OutHit.HitComponent = Scene.FindPrimitiveComponent(Proxy);
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			UPrimitiveComponent* Component = Scene.FindPrimitiveComponent(Proxy);

			if (!Component || !Component->IsVisible())
				return false;

			if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
			{
				if (!ResolveBillboard)
				{
					FHitResult Hit;
					if (!Billboard->LineTraceComponent(WorldRay, Hit) ||
						Hit.Distance >= InOutNearestT)
					{
						return false;
					}

					OutHit = Hit;
					InOutNearestT = Hit.Distance;
					return true;
				}

				const FMatrix BillboardToWorld = ResolveBillboard(*Billboard, ViewContext);

				const FRay LocalRay = ToLocalRay(WorldRay, BillboardToWorld);

				float T = InOutNearestT;
				if (!Billboard->LineTraceComponentLocal(LocalRay, T))
				{
					return false;
				}

				OutHit.HitComponent = Billboard;
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
			const FRay LocalRay{
				.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
				.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
			};

			float T = InOutNearestT;
			if (!Component->LineTraceComponentLocal(LocalRay, T))
			{
				return false;
			}

			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
			InOutNearestT = T;
			return true;
		};

	const FPreparedRay PreparedRay(WorldRay);

	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT) { return RayIntersectsAABB(PreparedRay, Bounds.Min, Bounds.Max, OutEnterT); },
		[&](FPrimitiveSceneProxy* Proxy, float& OutNearestT) { return TraceComponent(Proxy, OutNearestT); },
		NearestT);

	return OutHit.HitComponent != nullptr;
}

void UWorld::BeginPlay()
{
	if (!IsGameWorld() || bBegunPlay) return;
	UClass* ModeClass = FindClass(GameModeClassName);
	if (!ModeClass || !ModeClass->IsChildOf(AGameMode::StaticClass()) || !ModeClass->Constructor)
		ModeClass = AGameMode::StaticClass();
	GameMode = Cast<AGameMode>(SpawnActor(ModeClass));
	if (!GameMode) return;
	if (!PlayerPawn && PlayerPawnClassName.empty())
	{
		for (AActor* Actor : PersistentLevel->GetActors())
		{
			if (APawn* Pawn = Cast<APawn>(Actor)) { PlayerPawn = Pawn; break; }
		}
	}
	GameMode->SpawnPlayer(PlayerPawn, PlayerController);
	if (PlayerController)
	{
		UClass* HUDClass = GetHUDClass();
		HUD = Cast<AGameHUD>(SpawnActor(HUDClass));
		if (HUD)
		{
			HUD->SetOwningPlayerController(PlayerController);
			PlayerController->SetHUD(HUD);
		}
	}

	// 기존 액터 목록을 고정한 후 시작한다. BeginPlay 중 생성된 액터는 대기열로 간다.
	TArray<AActor*> InitialActors;
	for (ULevel* Level : Levels)
		if (Level)
			for (AActor* Actor : Level->GetActors())
				if (Actor) InitialActors.Add(Actor);
	bBegunPlay = true;
	for (AActor* Actor : InitialActors)
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->BeginPlay();

	// BeginPlay 중 생성된 액터도 PIE 입력 전에 시작한다.
	while (!BeginPlayList.IsEmpty())
	{
		AActor* Actor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->BeginPlay();
	}
}

void UWorld::EndPlay()
{
	bBegunPlay = false;
	TArray<AActor*> EndingActors;
	for (ULevel* Level : Levels)
		if (Level)
			for (AActor* Actor : Level->GetActors())
				if (IsValid(Actor) && Actor->HasBegunPlay()) EndingActors.Add(Actor);
	for (AActor* Actor : EndingActors)
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->EndPlayActor();
	if (HUD) HUD->HideWidget();
	if (PlayerController)
	{
		PlayerController->SetHUD(nullptr);
		PlayerController->ClearInputState();
		PlayerController->UnPossess();
	}
	PlayerController = nullptr;
	PlayerPawn = nullptr;
	GameMode = nullptr;
	HUD = nullptr;
	bIsPaused = false;
}

namespace
{
	UClass* ResolveGameClass(const FString& Name, UClass* Base, UClass* Fallback)
	{
		UClass* Class = Name.empty() ? nullptr : FindClass(Name);
		return Class && Class->IsChildOf(Base) && Class->Constructor ? Class : Fallback;
	}
}

UClass* UWorld::GetDefaultPawnClass() const
{
	return ResolveGameClass(PlayerPawnClassName, APawn::StaticClass(),
		GameMode ? GameMode->GetDefaultPawnClass() : ADefaultPawn::StaticClass());
}

UClass* UWorld::GetPlayerControllerClass() const
{
	return ResolveGameClass(PlayerControllerClassName, APlayerController::StaticClass(),
		GameMode ? GameMode->GetPlayerControllerClass() : APlayerController::StaticClass());
}

UClass* UWorld::GetHUDClass() const
{
	return ResolveGameClass(HUDClassName, AGameHUD::StaticClass(),
		GameMode ? GameMode->GetHUDClass() : AGameHUD::StaticClass());
}

bool UWorld::SetGameModeClass(UClass* Class)
{
	if (!Class || !Class->IsChildOf(AGameMode::StaticClass()) || !Class->Constructor) return false;
	if (GameModeClassName == Class->Name) return true;
	GameModeClassName = Class->Name;
	PlayerPawnClassName.clear();
	PlayerControllerClassName.clear();
	HUDClassName.clear();
	return true;
}

bool UWorld::SetPlayerPawnClass(UClass* Class)
{
	if (Class && (!Class->IsChildOf(APawn::StaticClass()) || !Class->Constructor)) return false;
	PlayerPawnClassName = Class ? Class->Name : "";
	return true;
}

bool UWorld::SetPlayerControllerClass(UClass* Class)
{
	if (Class && (!Class->IsChildOf(APlayerController::StaticClass()) || !Class->Constructor)) return false;
	PlayerControllerClassName = Class ? Class->Name : "";
	return true;
}

bool UWorld::SetHUDClass(UClass* Class)
{
	if (Class && (!Class->IsChildOf(AGameHUD::StaticClass()) || !Class->Constructor)) return false;
	HUDClassName = Class ? Class->Name : "";
	return true;
}

UWorld* UWorld::CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName)
{
	UWorld* NewWorld = NewObject<UWorld>(nullptr, WorldName);
	NewWorld->SetFlags(EObjectFlags::RF_Transactional);
	NewWorld->WorldType = InWorldType;
	NewWorld->Init();

	return NewWorld;
}

UWorld* UWorld::GetDuplicatedWorldForPIE(UWorld * InWorld)
{
	if (!InWorld)
	{
		return nullptr;
	}

	UWorld* PIEWorld = CreateWorld(EWorldType::PIE, false);

	FObjectDuplicationParameters Params(InWorld, nullptr);
	Params.DuplicationSeed.Add(InWorld, PIEWorld);
	Params.DuplicationSeed.Add(InWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
	Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;

	StaticDuplicateObjectEx(Params);
	return PIEWorld;
}

void UWorld::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		assert(PersistentLevel);

		// Update the persistent level's owning world. This is needed for some initialization
		if (!PersistentLevel->OwningWorld)
		{
			PersistentLevel->OwningWorld = this;
		}
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (!Actor)
			continue;

		for (UActorComponent* Component : Actor->GetComponents())
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
				SceneComp->MarkTransformDirty();

		Actor->RegisterAllComponents();

	}

	Scene.BuildBVH();
}

void UWorld::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();
	PersistentLevel->Serialize(Record.EnterRecord("PersistentLevel"));
}

void UWorld::UpdateOverlaps()
{
	const auto IsActiveShape = [this](UShapeComponent* Shape)
	{
		return IsValid(Shape) && Shape->IsRegistered() && Shape->GetWorld() == this &&
			IsValid(Shape->GetOwner()) && !Shape->GetOwner()->IsActorBeingDestroyed();
	};
	// 1. 이 월드에서 살아 있고 등록된 모든 충돌 도형을 수집한다.
	// bGenerateOverlapEvents가 false인 도형도 넣는다.
	// 그래야 실행 중 이벤트를 껐을 때 기존 Overlap에 End를 보낼 수 있다.
	TArray<UShapeComponent*> Shapes;

	for (TObjectIterator<UShapeComponent> It; It; ++It)
	{
		UShapeComponent* Shape = *It;

		if (!IsActiveShape(Shape))
		{
			continue;
		}

		Shapes.Add(Shape);
	}

	// 2. 도형은 렌더용 Scene.BVH에 없으므로 이번 프레임의 도형 전용 BVH를 만든다.
	std::vector<int32> Indices;
	std::unordered_map<UPrimitiveComponent*, int32> ShapeIndices;
	Indices.reserve(Shapes.Num());
	ShapeIndices.reserve(Shapes.Num());
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		Indices.push_back(i);
		ShapeIndices.emplace(Shapes[i], i);
	}

	TBVH<int32> OverlapBVH([&](const int32& Index) -> FBox
		{
			return Shapes[Index]->GetWorldAABB();
		});
	OverlapBVH.Build(std::span<const int32>(Indices.data(), Indices.size()));

	std::vector<int32> Candidates;
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		UShapeComponent* A = Shapes[i];
		if (!IsActiveShape(A)) continue;
		AActor* OwnerA = A->GetOwner();

		Candidates.clear();
		const FBox QueryBounds = A->GetWorldAABB();
		OverlapBVH.Query(
			[&](const FBox& Bounds) { return AreAABBsOverlapping(QueryBounds, Bounds); },
			[&](int32 j)
			{
				if (j > i) Candidates.push_back(j);
			});

		// BVH 후보에서 빠진 이전 겹침도 검사해야 분리/충돌 설정 해제 시 End를 보낼 수 있다.
		for (const FOverlapInfo& Info : A->GetOverlapInfos())
		{
			const auto It = ShapeIndices.find(Info.OtherComp);
			if (It != ShapeIndices.end() && It->second > i)
				Candidates.push_back(It->second);
		}

		// 후보 중복을 없애고 기존 i < j 순서대로 이벤트를 전달한다.
		std::sort(Candidates.begin(), Candidates.end());
		Candidates.erase(std::unique(Candidates.begin(), Candidates.end()), Candidates.end());

		for (int32 j : Candidates)
		{
			if (!IsActiveShape(A)) break;
			UShapeComponent* B = Shapes[j];
			if (!IsActiveShape(B)) continue;
			AActor* OwnerB = B->GetOwner();

			// 같은 Actor의 콜라이더끼리는 여기서는 무시한다.
			// 필요하면 나중에 self-collision 정책을 별도로 추가한다.
			const bool bCanGenerateOverlap =
				OwnerA &&
				OwnerB &&
				OwnerA != OwnerB &&
				OwnerA->GetActorEnableCollision() &&
				OwnerB->GetActorEnableCollision() &&
				A->bGenerateOverlapEvents &&
				B->bGenerateOverlapEvents;

			bool bIsOverlappingNow = false;

			if (bCanGenerateOverlap)
			{
				// 3. 값싼 AABB 검사에서 먼저 탈락시킨다.
				const FBox BoundsA = A->GetWorldAABB();
				const FBox BoundsB = B->GetWorldAABB();

				if (AreAABBsOverlapping(BoundsA, BoundsB))
				{
					// 4. 실제 도형 충돌: Sphere/Box/Capsule 조합
					bIsOverlappingNow = FCollision::Overlap(A, B);
				}
			}

			// 5. 지난 프레임의 상태
			const bool bWasOverlapping =
				A->IsOverlappingComponent(B);

			// false -> true: 첫 접촉
			if (!bWasOverlapping && bIsOverlappingNow)
			{
				A->BeginComponentOverlap(B);
				if (!IsActiveShape(A) || !IsActiveShape(B)) continue;
				B->BeginComponentOverlap(A);
				if (!IsActiveShape(A) || !IsActiveShape(B)) continue;

				// 지금은 “처음 막혔을 때 한 번” Hit를 보내는 정책
				if (A->bBlockComponent && B->bBlockComponent)
				{
					A->DispatchHit(B);
					if (IsActiveShape(A) && IsActiveShape(B)) B->DispatchHit(A);
				}
			}
			// true -> false: 분리
			else if (bWasOverlapping && !bIsOverlappingNow)
			{
				A->EndComponentOverlap(B);
				if (IsActiveShape(A) && IsActiveShape(B)) B->EndComponentOverlap(A);
			}
		}
	}
}

float UWorld::GetEffectiveTimeDilation() const
{
	const float HitStop = (HitStopTimeRemaining > 0.0f) ? HitStopDilation : 1.0f;
	// UE AWorldSettings::GetEffectiveTimeDilation: TimeDilation * CinematicTimeDilation (* DemoPlay)
	return FMath::Clamp(GlobalTimeDilation * CinematicTimeDilation * HitStop, MinTimeDilation, MaxTimeDilation);
}

void UWorld::StartHitStop(float Duration, float Dilation)
{
	HitStopTimeRemaining = std::max(HitStopTimeRemaining, Duration);
	HitStopDilation = Dilation;
}
