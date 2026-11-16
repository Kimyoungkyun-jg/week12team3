#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"

struct FDecalReceiverItemRef
{
	uint32 CommandIndex = 0;
};

inline bool operator<(
	const FDecalReceiverItemRef& A, const FDecalReceiverItemRef& B)
{
	return A.CommandIndex < B.CommandIndex;
}

inline bool operator==(
	const FDecalReceiverItemRef& A, const FDecalReceiverItemRef& B)
{
	return A.CommandIndex == B.CommandIndex;
}

struct FProjectedDecalMeshDrawList
{
	TArray<FDecalReceiverItemRef> Items;
};
