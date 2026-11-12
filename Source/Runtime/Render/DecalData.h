#pragma once

struct alignas(16) FDecalData
{
	FMatrix VP;
	float Fade;
	float bAdditive;
	float Padding[2];
};

static_assert(sizeof(FDecalData) == 80);

