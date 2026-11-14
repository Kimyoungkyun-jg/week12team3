#pragma once

#include "Math/Vector.h"
#include "Math/Color.h"

struct FBaseParticle
{
	FVector Location{ 0.0f, 0.0f, 0.0f };
	FVector Velocity{ 0.0f, 0.0f, 0.0f };
	float RelativeTime = 0.0f;
	float Lifetime = 2.0f;

	FVector BaseVelocity{ 0.0f, 0.0f, 0.0f };
	float Rotation = 0.0f;
	float RotationRate = 0.0f;

	FVector Size{ 1.0f, 1.0f, 1.0f };
	FColor Color{ 255, 255, 255, 255 };
	uint8 BaseAlpha = 255;
};
