#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"

struct FDecalReceiverItemRef
{
	uint32 BatchIndex = 0;
	uint32 ItemIndex = 0;
};

inline bool operator<(
	const FDecalReceiverItemRef& A, const FDecalReceiverItemRef& B)
{
	if (A.BatchIndex != B.BatchIndex) 
		return A.BatchIndex < B.BatchIndex;

	return A.ItemIndex < B.ItemIndex;
}

inline bool operator==(
	const FDecalReceiverItemRef& A, const FDecalReceiverItemRef& B)
{
	return A.BatchIndex == B.BatchIndex && A.ItemIndex == B.ItemIndex;
}

struct FProjectedDecalMeshDrawList
{
	// 위치만 저장
	TArray<FDecalReceiverItemRef> Items;
};