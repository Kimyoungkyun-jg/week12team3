#include "EnginePCH.h"

#include "LuaActorRef.h"

#include <sol/sol.hpp>

#include "GameFrameWork/Actor.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"

FLuaActorRef FLuaActorRef::Make(AActor* Actor)
{
	FLuaActorRef Ref;
	if (Actor)
	{
		Ref.World = Actor->GetWorld();
		Ref.WorldUUID = Ref.World ? Ref.World->GetUUID() : 0;
		Ref.Ptr = Actor;
		Ref.UUID = Actor->GetUUID();
	}
	return Ref;
}

AActor* FLuaActorRef::Get() const
{
	uint32 LiveWorldUUID = 0;
	if (!World || !Ptr || !TryGetLiveObjectUUID(World, UWorld::StaticClass(), LiveWorldUUID) ||
		LiveWorldUUID != WorldUUID || !IsValid(World))
	{
		return nullptr;
	}

	for (ULevel* Level : World->GetLevels())
	{
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor == Ptr)
			{
				return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Actor->GetUUID() == UUID ? Actor : nullptr;
			}
		}
	}

	return nullptr;
}

AActor& Deref(const FLuaActorRef& Ref)
{
	AActor* Actor = Ref.Get();
	if (!Actor)
	{
		throw sol::error(std::format("Attempt to access a destroyed or invalid actor reference: {}", Ref.UUID));
	}
	return *Actor;
}

FLuaComponentRef FLuaComponentRef::Make(UActorComponent* Component)
{
	FLuaComponentRef Ref;
	if (Component)
	{
		Ref.Owner = Component->GetOwner();
		Ref.World = Ref.Owner ? Ref.Owner->GetWorld() : nullptr;
		Ref.WorldUUID = Ref.World ? Ref.World->GetUUID() : 0;
		Ref.Ptr = Component;
		Ref.UUID = Component->GetUUID();
	}
	return Ref;
}

UActorComponent* FLuaComponentRef::Get() const
{
	uint32 LiveWorldUUID = 0;
	if (!World || !Ptr || !TryGetLiveObjectUUID(World, UWorld::StaticClass(), LiveWorldUUID) ||
		LiveWorldUUID != WorldUUID || !IsValid(World))
	{
		return nullptr;
	}
	for (ULevel* Level : World->GetLevels())
	{
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor == Owner && IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (Component == Ptr)
					{
						return IsValid(Component) && Component->GetUUID() == UUID ? Component : nullptr;
					}
				}
			}
		}
	}
	return nullptr;
}

UActorComponent& Deref(const FLuaComponentRef& Ref)
{
	UActorComponent* Component = Ref.Get();
	if (!Component)
	{
		throw sol::error(std::format("Attempt to access a destroyed or invalid component reference: {}", Ref.UUID));
	}
	return *Component;
}
