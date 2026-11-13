#pragma once 

#include "GameFramework/Actor.h"
#include "Components/DecalComponent.h"
#include "Components/BillboardComponent.h"

#include "DecalActor.generated.h"

UCLASS()
class ADecalActor : public AActor
{
	GENERATED_BODY()

public:
	ADecalActor();
	~ADecalActor() override = default;
	void Tick(float DeltaTime) override;
	void SetTemporaryLifetime(float Seconds);

	UDecalComponent* GetDecalComponent() const
	{
		return DecalComponent;
	}

	UBillboardComponent* GetBillboardComponent() const
	{
		return BillboardComponent;
	}

private:
	float RemainingLifetime = 0.0f;
	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
