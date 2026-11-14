#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Color.h"

struct FBaseParticle
{
	FVector Location{ 0.0f, 0.0f, 0.0f };
	float Size = 0.0f;
	float BaseSize = 0.0f;
	FLinearColor Color{ 0.0f, 0.0f, 0.0f, 0.0f };
	FVector Velocity = { 0.0f, 0.0f, 0.0f };
	
	float RelativeTime = 0.0f;
	float Lifetime = 0.0f;
};