#include "EnginePCH.h"
#include "SkeletalMeshActor.h"
#include "Render/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpace1D.h"

namespace
{
    // A의 임포터가 붙기 전까지 쓰는 테스트 막대. 에셋은 하나만 만들어 모든 액터가 공유한다
    USkeletalMesh* GetTestBoneStickMesh()
    {
        static USkeletalMesh* Cached = nullptr;
        if (Cached) return Cached;

        USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
        FString Error;
        if (Mesh->Build(MakeTestBoneStick(3, 1.0f), Error))
            Cached = Mesh;   // 실패하면(렌더러 초기화 전 등) 다음 생성 때 다시 시도한다
        return Cached;
    }
}

ASkeletalMeshActor::ASkeletalMeshActor()
{
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("USkeletalMeshComponent");
    SkeletalMeshComponent->SetupAttachment(GetRootComponent());
    SkeletalMeshComponent->SetSkeletalMesh(GetTestBoneStickMesh());
}
