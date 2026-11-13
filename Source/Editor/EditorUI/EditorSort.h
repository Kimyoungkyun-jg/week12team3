#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

// 에디터 드롭다운 목록을 알파벳순으로 보여 주기 위한 정렬 도우미.
namespace EditorSort
{
// 대소문자를 무시하고 비교한다. 대소문자만 다르면 원래 문자열 순서로 정해 결과가 항상 같다.
inline bool LessIgnoreCase(const std::string& A, const std::string& B)
{
	const size_t Count = std::min(A.size(), B.size());
	for (size_t Index = 0; Index < Count; ++Index)
	{
		const int CharA = std::tolower(static_cast<unsigned char>(A[Index]));
		const int CharB = std::tolower(static_cast<unsigned char>(B[Index]));
		if (CharA != CharB) return CharA < CharB;
	}
	if (A.size() != B.size()) return A.size() < B.size();
	return A < B;
}

// 컨테이너를 GetName(항목)이 돌려준 이름의 알파벳순으로 정렬한다.
template <typename ContainerType, typename NameGetterType>
void SortByName(ContainerType& Items, NameGetterType&& GetName)
{
	std::stable_sort(Items.begin(), Items.end(), [&GetName](const auto& A, const auto& B)
	{
		return LessIgnoreCase(GetName(A), GetName(B));
	});
}

// TObjectIterator 결과를 모아 이름순 배열로 만든다. Filter가 false인 객체는 빠진다.
template <typename ObjectType, typename IteratorType, typename NameGetterType, typename FilterType>
TArray<ObjectType*> CollectSorted(IteratorType It, NameGetterType&& GetName, FilterType&& Filter)
{
	TArray<ObjectType*> Result;
	for (; It; ++It)
	{
		ObjectType* Object = *It;
		if (Object && Filter(Object)) Result.Add(Object);
	}
	SortByName(Result, GetName);
	return Result;
}
}
