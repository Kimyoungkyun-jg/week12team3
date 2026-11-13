#include "EnginePCH.h"
#include "Level.h"
#include "World.h"

// delete Actor가 소멸자를 호출하려면 AActor의 완전한 정의가 필요하다.
// 전방 선언만으로는 C4150(소멸자 미호출)이 되어 액터가 GUObjectArray에 남는다.
#include "GameFramework/Actor.h"
#include "Components/ExponentialHeightFogComponent.h"

void ULevel::AddActor(AActor* Actor)
{
	if (!Actor)
		return;

	Actors.Add(Actor);
}

void ULevel::ClearActors()
{
	for (AActor* Actor : Actors)
	{
		if (!Actor)
			continue;

		delete Actor;
	}
	Actors.Reset();
}

void ULevel::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Actors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		if (Ar.IsLoading())
			Actors.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Actors[i];
			ActorArray.EnterElement() << Obj;
			Actors[i] = static_cast<AActor*>(Obj);
		}

		UObject* WorldObj = OwningWorld;
		Record << SA_VALUE("OwningWorld", WorldObj);
		OwningWorld = static_cast<UWorld*>(WorldObj);
		return;
	}

	if (Ar.IsSaving())
	{
		// 개수를 먼저 알아야 EnterArray를 할 수 있으므로 저장할 액터부터 고른다
		TArray<AActor*> SavedActors;
		for (AActor* Actor : Actors)
		{
			if (Actor && !Actor->HasAnyFlags(EObjectFlags::RF_Transient))
				SavedActors.Add(Actor);
		}

		int32 Num = SavedActors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		for (AActor* Actor : SavedActors)
			SaveActorRecord(ActorArray.EnterElement().EnterRecord(), Actor);
		return;
	}

	// 불러오기: 월드를 비우는 건 호출하는 쪽 책임 (PIE 복제는 새 월드라 비울 필요가 없다)
	assert(OwningWorld);

	int32 Num = 0;
	FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
	TArray<FPendingActorAttachment> PendingAttachments;
	TArray<AActor*> Spawned;
	TArray<FString> SavedNames;
	for (int32 i = 0; i < Num; ++i)
	{
		FString SavedName;
		if (AActor* Actor = SpawnActorFromRecord(ActorArray.EnterElement().EnterRecord(), &PendingAttachments, &SavedName))
		{
			Spawned.Add(Actor);
			SavedNames.Add(SavedName);
		}
	}
	ResolveActorAttachments(PendingAttachments, Spawned, SavedNames);
}

void ULevel::ResolveActorAttachments(const TArray<FPendingActorAttachment>& Pending, const TArray<AActor*>& Spawned, const TArray<FString>& SavedNames)
{
	for (const FPendingActorAttachment& Attachment : Pending)
	{
		AActor* Parent = nullptr;
		for (int32 Index = 0; Index < Spawned.Num(); ++Index)
		{
			if (SavedNames[Index] == Attachment.ParentActorName)
			{
				Parent = Spawned[Index];
				break;
			}
		}
		// 함께 불러오지 않은 부모(일부만 붙여넣기)는 레벨에 있는 같은 이름의 액터에 붙인다.
		if (!Parent)
			Parent = Cast<AActor>(StaticFindObjectFast(nullptr, this, FName(Attachment.ParentActorName)));
		if (!Parent || Parent == Attachment.Child)
			continue;

		USceneComponent* ParentComponent = Cast<USceneComponent>(Parent->FindComponentByName(FName(Attachment.ParentComponentName)));
		// 저장된 루트 Transform은 부모 기준이므로 KeepRelative로 붙인다.
		if (!Attachment.Child->AttachToActor(Parent, EAttachmentRule::KeepRelative, ParentComponent))
			HTR_LOG(Warning, "Load: cannot attach {} to {}", Attachment.Child->GetName(), Parent->GetName());
	}
}

void ULevel::SaveActorRecord(FStructuredArchive::FRecord ActorRecord, AActor* Actor)
{
	FString ClassName = Actor->GetClass()->Name;
	FString ActorName = Actor->GetName();
	ActorRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ActorName);
	Actor->Serialize(ActorRecord);

	// 다른 액터에 붙어 있으면 부모 이름을 남긴다. 붙지 않은 액터는 필드를 쓰지 않아 예전 씬과 형식이 같다.
	USceneComponent* Root = Actor->GetRootComponent();
	USceneComponent* AttachParent = Root ? Root->GetAttachParent() : nullptr;
	AActor* ParentActor = Actor->GetAttachParentActor();
	if (TOptional<FStructuredArchive::FSlot> AttachSlot = ActorRecord.TryEnterField("AttachParent", ParentActor != nullptr))
	{
		FString ParentActorName = ParentActor->GetName();
		FString ParentComponentName = AttachParent->GetName();
		FStructuredArchive::FRecord AttachRecord = AttachSlot.GetValue().EnterRecord();
		AttachRecord << SA_VALUE("Actor", ParentActorName) << SA_VALUE("Component", ParentComponentName);
	}
}

AActor* ULevel::SpawnActorFromRecord(FStructuredArchive::FRecord ActorRecord, TArray<FPendingActorAttachment>* OutAttachments, FString* OutSavedName)
{
	assert(OwningWorld);

	FString ClassName;
	FString ActorName;
	ActorRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ActorName);   // 없으면 빈 문자열

	UClass* Class = FindClass(ClassName);
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		HTR_LOG(Warning, "Load: unknown actor class '{}', skipped", ClassName);
		return nullptr;
	}
	// 같은 이름이 이미 있으면 그대로 생성할 때 앞서 만든 액터를 덮어쓰게 되므로 새 이름을 받는다 (붙여넣기는 항상 여기로 온다)
	FName Name = ActorName.empty() ? NAME_None : FName(ActorName);
	if (!Name.IsNone() && StaticFindObjectFast(nullptr, this, Name))
		Name = NAME_None;

	AActor* Actor = OwningWorld->SpawnActor(Class, Name);
	if (!Actor)
		return nullptr;
	Actor->Serialize(ActorRecord);
	if (OutSavedName)
		*OutSavedName = ActorName;

	if (TOptional<FStructuredArchive::FSlot> AttachSlot = ActorRecord.TryEnterField("AttachParent", false))
	{
		FPendingActorAttachment Attachment;
		Attachment.Child = Actor;
		FStructuredArchive::FRecord AttachRecord = AttachSlot.GetValue().EnterRecord();
		AttachRecord << SA_VALUE("Actor", Attachment.ParentActorName) << SA_VALUE("Component", Attachment.ParentComponentName);
		if (OutAttachments)
			OutAttachments->Add(Attachment);
	}
	return Actor;
}
