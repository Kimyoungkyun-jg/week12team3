#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "SkyLightActor.h"

ASkyLightActor::ASkyLightActor()
{
    SkyLightComponent = CreateDefaultSubobject<USkyLightComponent>("USkyLightComponent");
	SetupLightRoot(SkyLightComponent, ResourcePaths::SkyLightIcon);
}