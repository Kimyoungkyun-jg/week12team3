#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Containers/Array.h"
#include "Level.generated.h"

class UWorld;
class AActor;

UCLASS()
class ULevel : public UObject
{
    GENERATED_BODY()

public:
    ULevel() = default;
    virtual ~ULevel() = default;

    UWorld* GetWorld() const { return OwningWorld; }
    void SetWorld(UWorld* InWorld) { OwningWorld = InWorld; }

    const TArray<AActor*>& GetActors() const  { return Actors; }
    uint32 GetActorNum() const { return Actors.Num(); }

    void AddActor(AActor* Actor);
    void ClearActors();

    using Super::Serialize;   
    virtual void Serialize(FStructuredArchive::FRecord Record) override;

    // 액터 하나를 씬 형식(Class, Name, 속성)으로 쓰고 읽는다. 씬 저장과 에디터 복사·붙여넣기가 같이 쓴다 (TEAM1).
    static void SaveActorRecord(FStructuredArchive::FRecord ActorRecord, AActor* Actor);
    // 다른 액터에 붙은 액터의 부모 정보 (TEAM5 AttachParent). 부모는 함께 불러온 액터 중에서 저장 당시 이름으로 먼저 찾는다.
    struct FPendingActorAttachment
    {
        AActor* Child = nullptr;
        FString ParentActorName;
        FString ParentComponentName;
    };
    // SpawnActorFromRecord가 모은 부착 정보를 적용한다. SavedNames[i]는 Spawned[i]의 저장 당시 이름이다.
    void ResolveActorAttachments(const TArray<FPendingActorAttachment>& Pending, const TArray<AActor*>& Spawned, const TArray<FString>& SavedNames);
    // 레코드로 새 액터를 만든다. 저장된 이름이 이미 있으면(붙여넣기 등) 새 이름을 받는다.
    AActor* SpawnActorFromRecord(FStructuredArchive::FRecord ActorRecord, TArray<FPendingActorAttachment>* OutAttachments = nullptr, FString* OutSavedName = nullptr);

    //virtual void Serialize(FArchive& Ar) override; // Save Level 구현예정

private:
    UWorld* OwningWorld = nullptr;
    TArray<AActor*> Actors;

    // AWorldSettings* WorldSettings = nullptr;

    bool bIsVisible = true;

    friend class UWorld;
};