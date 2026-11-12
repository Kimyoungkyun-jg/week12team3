#pragma once

#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "RootMotionTestCharacter.generated.h"

class UTextRenderComponent;

// TEMP: 루트 모션 시연용 캐릭터. 메시와 애니메이션은 SkeletalMeshComponent에 지정한 AnimBP(.animblueprint)가 정한다.
// Root Motion Mode도 AnimBP Class Defaults에 저장된 값을 쓴다 (Unreal과 같음). 머리 위 라벨은 그 모드 이름이다.
UCLASS()
class ARootMotionTestCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARootMotionTestCharacter();
	void Tick(float DeltaTime) override;

private:
	void UpdateModeLabel();

	UTextRenderComponent* LabelComponent = nullptr;      // 머리 위 Root Motion Mode 이름
	UTextRenderComponent* RootLabelComponent = nullptr;  // root 본을 따라다니는 "root" 표시
	int32 ShownMode = -1;                                // 라벨에 마지막으로 쓴 모드 (바뀔 때만 SetText)
};
