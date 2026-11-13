#pragma once

#include "Math/Vector.h"
#include "Math/Color.h"

struct FBaseParticle
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float RelativeTime = 0.0f;
	float Lifetime = 1.0f;
	FVector BaseVelocity = FVector::ZeroVector;
	float Rotation = 0.0f;
	float RotationRate = 0.0f;
	FVector Size = FVector(40.0f, 40.0f, 1.0f);
	FLinearColor Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
};