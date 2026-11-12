#pragma once

#include "Box.h"
#include "Frustum.h"

struct FOBBTreeNode {
	FBox LocalBounds;
	int32 Left = -1;
	int32 Right = -1;
	uint32 FirstTriangle = 0;
	uint32 TriangleCount = 0;

	bool IsLeaf() const { return Left < 0; }
};

class FOBBTree {
public:
	void Build(const TArray <FVector>& Positions, const TArray<uint32>& Indices);
	const TArray <FOBBTreeNode> & GetNodes() const { return Nodes; }

	bool Intersects(const FOBB& DecalWorld, const FMatrix& LocalToWorld) const;

private:
	int32 BuildNode(uint32 First, uint32 Count, uint32 Depth);

private:
	TArray <FOBBTreeNode> Nodes;
	TArray <uint32> Triangles;

	static constexpr uint32 MaxDepth = 8;
	static constexpr uint32 MinTriangles = 8;

	TArray<FBox> TriangleBounds;
	TArray<FVector> Centroids;	// 삼각형 무게중심 모음

	// 질의마다 새로 할당하지 않도록 멤버로 둔다 (TBVH의 QueryStack과 같은 방식)
	mutable TArray<int32> Stack;
};