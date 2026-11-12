#include "EnginePCH.h"
#include "SkyLightActor.h"

ASkyLightActor::ASkyLightActor()
{
    SkyLightComponent = CreateDefaultSubobject<USkyLightComponent>("USkyLightComponent");
	SetupLightRoot(SkyLightComponent, "Engine/Icons/Sky.png");
}