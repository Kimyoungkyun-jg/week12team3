#include "EnginePCH.h"
#include "GameFramework/MyPlayerController.h"
#include "GameFramework/Actor/MyCharacter.h"

void AMyPlayerController::InputKey(int32 Key, bool bDown)
{
	// Super가 bKeyDown을 갱신하므로, 눌림 순간 판정은 호출 전에 이전 상태를 잡아 둔다
	const bool bJustPressed = bDown && !IsKeyDown(Key);
	Super::InputKey(Key, bDown);

	if (!bJustPressed) return;

	if (Key == static_cast<int32>(EKeyCode::LButton))
	{
		if (AMyCharacter* MyCharacter = Cast<AMyCharacter>(GetPawn()))
		{
			MyCharacter->Attack();
		}
	}

	if (Key == static_cast<int32>(EKeyCode::Space))
	{
		if (AMyCharacter* MyCharacter = Cast<AMyCharacter>(GetPawn()))
		{
			MyCharacter->Jump();
		}
	}
}
