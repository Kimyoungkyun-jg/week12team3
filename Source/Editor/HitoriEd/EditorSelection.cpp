#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorSelection.h"

void FEditorSelection::Select(AActor* Actor)
{
	const bool bAlreadyOnlySelection = Actors.Num() == 1 && Actors[0] == Actor;
	if (bAlreadyOnlySelection || (!Actor && Actors.IsEmpty()))
		return;

	Actors.Reset();
	if (Actor)
		Actors.Add(Actor);
	NotifyChanged();
}

void FEditorSelection::Toggle(AActor* Actor)
{
	if (!Actor)
		return;

	int32 Index = 0;
	if (Actors.Find(Actor, Index))
		Actors.RemoveAt(Index, 1);
	else
		Actors.Add(Actor);
	NotifyChanged();
}

void FEditorSelection::SelectActors(const TArray<AActor*>& InActors)
{
	Actors.Reset();
	for (AActor* Actor : InActors)
	{
		if (Actor && !IsSelected(Actor))
			Actors.Add(Actor);
	}
	NotifyChanged();
}

void FEditorSelection::Remove(AActor* Actor)
{
	int32 Index = 0;
	if (!Actor || !Actors.Find(Actor, Index))
		return;

	Actors.RemoveAt(Index, 1);
	NotifyChanged();
}

void FEditorSelection::Clear()
{
	if (Actors.IsEmpty())
		return;

	Actors.Reset();
	NotifyChanged();
}

bool FEditorSelection::IsSelected(const AActor* Actor) const
{
	for (const AActor* Selected : Actors)
	{
		if (Selected == Actor)
			return true;
	}
	return false;
}

void FEditorSelection::NotifyChanged() const
{
	if (OnChanged)
		OnChanged();
}
