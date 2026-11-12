#include "EnginePCH.h"
#include "AnimNotify.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameplayStatics.h"
#include "Asset/AssetManager.h"
#include "Engine/Classes/Sound/SoundWave.h"
#include "AnimSequenceBase.h"

// 엔진 종료 때 DestroyAllObjectsExceptEngine이 UObject를 하나씩 지우므로
// Notify가 시퀀스보다 먼저 지워질 수 있다. 시퀀스가 지운 포인터를 다시 지우지 않게 알린다
UAnimNotify::~UAnimNotify()
{
	if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(GetOuter())) Sequence->OnNotifyObjectDestroyed(this);
}

void UAnimNotify_PlaySound::Notify(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp || SoundPath.empty())
		return;

	UWorld* World = MeshComp->GetWorld();
	if (!World)
		return;

	// AssetManager가 경로별로 캐시하므로 처음 한 번만 디코딩한다
	USoundWave* Sound = UAssetManager::Get().LoadSound(SoundPath);
	if (!Sound)
	{
		HTR_LOG(Warning, "AnimNotify_PlaySound: sound load failed: {}", SoundPath);
		return;
	}

	// UE UAnimNotify_PlaySound처럼 메시 위치에서 재생한다 (bFollow 없음)
	UGameplayStatics::PlaySoundAtLocation(World, Sound, MeshComp->GetWorldLocation(), Volume);
}
