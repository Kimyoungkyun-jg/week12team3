#pragma once

#include "GameFramework/Actor.h"
#include "Components/DecalComponent.h"
#include "Components/BillboardComponent.h"

#include "FakeSpotLightActor.generated.h"

UCLASS()
class AFakeSpotLightActor : public AActor
{
	GENERATED_BODY()
public:
	AFakeSpotLightActor();
	~AFakeSpotLightActor() override = default;

	UDecalComponent* GetDecalComponent() const
	{
		return DecalComponent;
	}

	UBillboardComponent* GetFakeLightBillboardComponent() const
	{
		return FakeLightBillboardComponent;
	}

private:
	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* FakeLightBillboardComponent = nullptr;
	UBillboardComponent* IconBillboardComponent = nullptr;
};