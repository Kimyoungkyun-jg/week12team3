#pragma once

#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "Components/DecalComponent.h"
#include "DecalActor.generated.h"


UCLASS()
class ADecalActor : public AActor
{
    GENERATED_BODY()
public:
    ADecalActor();
    UDecalComponent* GetDecalComponent() const { return DecalComponent; }

private:
    UDecalComponent* DecalComponent = nullptr;
    UBillboardComponent* BillboardComponent = nullptr;
};