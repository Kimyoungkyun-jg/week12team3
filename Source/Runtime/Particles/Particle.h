#pragma once

#include "Math/Vector.h"
#include "Math/Color.h"

struct FBaseParticle
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Size = FVector(40.0f, 40.0f, 1.0f);
	FLinearColor Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	float RelativeTime = 0.0f;
	float OneOverMaxLifetime = 0.0f;
	bool bActive = true;
};