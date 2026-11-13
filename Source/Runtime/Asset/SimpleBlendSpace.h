#pragma once

#include "Asset/BlendSpace1D.h"
#include "SimpleBlendSpace.generated.h"

UCLASS()
class USimpleBlendSpace : public UBlendSpace1D
{
	GENERATED_BODY()
public:
	bool Configure(UAnimSequence* Idle, UAnimSequence* Walk, UAnimSequence* Jog)
	{
		return AddSample(0.0f, Idle) && AddSample(300.0f, Walk) && AddSample(600.0f, Jog);
	}
};
