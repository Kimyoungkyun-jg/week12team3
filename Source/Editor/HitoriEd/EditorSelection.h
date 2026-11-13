#pragma once

#include "Containers/Array.h"

#include <functional>

class AActor;

// 에디터에서 선택된 액터 목록. 마지막으로 선택한 액터가 기즈모·디테일의 기준(Primary)이다.
// 아웃라이너·뷰포트 피킹은 이 객체만 바꾸고, 바뀌면 OnChanged로 기즈모·디테일·외곽선을 다시 맞춘다.
class FEditorSelection
{
public:
	using FChangedCallback = std::function<void()>;
	void SetChangedCallback(FChangedCallback InCallback) { OnChanged = std::move(InCallback); }

	// 기존 선택을 지우고 Actor 하나만 선택한다. nullptr이면 선택을 비운다.
	void Select(AActor* Actor);
	// Actor가 선택돼 있으면 빼고, 아니면 Primary로 추가한다 (Shift·Ctrl 클릭).
	void Toggle(AActor* Actor);
	// 여러 액터를 한 번에 선택한다 (붙여넣기 결과 등). 마지막 액터가 Primary다.
	void SelectActors(const TArray<AActor*>& InActors);
	// 삭제되는 액터를 선택 목록에서 뺀다.
	void Remove(AActor* Actor);
	void Clear();

	bool IsSelected(const AActor* Actor) const;
	bool IsEmpty() const { return Actors.IsEmpty(); }
	AActor* GetPrimary() const { return Actors.IsEmpty() ? nullptr : Actors[Actors.Num() - 1]; }
	const TArray<AActor*>& GetActors() const { return Actors; }

private:
	void NotifyChanged() const;

	TArray<AActor*> Actors;
	FChangedCallback OnChanged;
};
