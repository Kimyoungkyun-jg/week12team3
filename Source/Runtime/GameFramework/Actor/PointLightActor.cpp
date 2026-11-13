#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "PointLightActor.h"

APointLightActor::APointLightActor()
{
    PointLightComponent = CreateDefaultSubobject<UPointLightComponent>("UPointLightComponent");
	SetupLightRoot(PointLightComponent, ResourcePaths::PointLightIcon);
}