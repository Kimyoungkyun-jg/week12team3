#include "EnginePCH.h"
#include "PointLightActor.h"

APointLightActor::APointLightActor()
{
    PointLightComponent = CreateDefaultSubobject<UPointLightComponent>("UPointLightComponent");
	SetupLightRoot(PointLightComponent, "Engine/Icons/Point.png");
}