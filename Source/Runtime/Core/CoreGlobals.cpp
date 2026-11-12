#include "EnginePCH.h"
#include "Core/CoreGlobals.h"

namespace
{
	bool GIsRequestingExit = false;
}

void RequestEngineExit(const FString& Reason)
{
	if (!GIsRequestingExit)
	{
		HTR_LOG(Info, "[Engine] exit requested: {}", Reason);
	}
	GIsRequestingExit = true;
}

bool IsEngineExitRequested()
{
	return GIsRequestingExit;
}
