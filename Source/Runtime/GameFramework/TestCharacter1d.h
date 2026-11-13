#pragma once

#include "GameFramework/TestCharacter.h"
#include "TestCharacter1d.generated.h"

UCLASS()
class ATestCharacter1d : public ATestCharacter
{
	GENERATED_BODY()
public:
	ATestCharacter1d();
protected:
	const char* GetAnimationGraphPath() const override { return "Assets/Animations/TestCharacter1d.animgraph"; }
};
