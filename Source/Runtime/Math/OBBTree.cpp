#include "EnginePCH.h"
#include "OBBTree.h"

namespace
{
	// 한 메시의 노드는 전부 같은 축(메시의 월드 축)에 정렬돼 있다.
	// 그래서 SAT 후보 축 15개와 데칼 쪽 값은 노드마다 다시 구할 필요가 없다.
	// 노드마다 달라지는 건 중심과 반지름뿐이라, 그 둘만 남겨 둔다.
	struct FSeparatingAxes
	{
		static constexpr int32 MaxAxes = 15;

		FVector Axis[MaxAxes];
		float DecalRadius[MaxAxes];		// 축에 투영한 데칼의 반지름
		float DecalCenter[MaxAxes];		// 축에 투영한 데칼의 중심
		FVector NodeScaledDot[MaxAxes];	// |메시 축 · 축| × 스케일. 노드 반지름에 그대로 곱한다
		int32 Count = 0;

		void Add(const FVector& L, const FOBB& Decal, const FVector* NodeAxis, const float* Scale)
		{
			if (Count >= MaxAxes) return;
			if (L.SizeSquared() < 1e-6f) return;	// 평행하면 영벡터라 분리를 못 찾는다

			Axis[Count] = L;
			DecalRadius[Count] = GetProjectedRadius(Decal, L);
			DecalCenter[Count] = Decal.Center | L;
			NodeScaledDot[Count] = FVector(
				std::abs(NodeAxis[0] | L) * Scale[0],
				std::abs(NodeAxis[1] | L) * Scale[1],
				std::abs(NodeAxis[2] | L) * Scale[2]);
			++Count;
		}
	};
}

void FOBBTree::Build(const TArray<FVector>& Positions, const TArray<uint32>& Indices)
{
	TriangleBounds.Reset();
	Centroids.Reset();
	Triangles.Reset();
	Nodes.Reset();

	if (Indices.Num() == 0) return;

	for (uint32 index = 0; index < Indices.Num() / 3; index++) {
		FVector V[3];
		V[0] = Positions[Indices[3 * index]];
		V[1] = Positions[Indices[3 * index + 1]];
		V[2] = Positions[Indices[3 * index + 2]];

		// 상자 리스트 추가
		FBox Box = { V[0], V[0] };
		Box.Expand({ V[1], V[1] });
		Box.Expand({ V[2], V[2] });
		TriangleBounds.Add(Box);

		// 무게중심 리스트 추가 
		FVector Centroid = (V[0] + V[1] + V[2]) / 3.0f;
		Centroids.Add(Centroid);

		// 삼각형 번호 추가
		Triangles.Add(index);
	}

	Stack.Reserve(MaxDepth * 2 + 2);	// 질의용 스택을 미리 잡아 둔다
	BuildNode(0, static_cast<uint32> (Triangles.Num()), 0);
}

bool FOBBTree::Intersects(const FOBB& DecalWorld, const FMatrix& LocalToWorld) const
{
	if (Nodes.Num() == 0) return true; 
	
	FVector Axis[3];
	float Scale[3];
	for (int32 i = 0; i < 3; i++) {
		const FVector Row(LocalToWorld[i][0], LocalToWorld[i][1], LocalToWorld[i][2]);
		Scale[i] = Row.Size();
		Axis[i] = Row / Scale[i];
	}

	// 후보 축 15개를 메시당 한 번만 구한다 (노드마다 다시 구하지 않는다)
	FSeparatingAxes Axes;
	for (int32 i = 0; i < 3; i++) Axes.Add(DecalWorld.Axis[i], DecalWorld, Axis, Scale);
	for (int32 i = 0; i < 3; i++) Axes.Add(Axis[i], DecalWorld, Axis, Scale);
	for (int32 i = 0; i < 3; i++) {
		for (int32 j = 0; j < 3; j++) {
			Axes.Add(DecalWorld.Axis[i] ^ Axis[j], DecalWorld, Axis, Scale);
		}
	}

	Stack.Reset();
	Stack.Add(0);
	while (!Stack.IsEmpty()) {
		const int32 nodeIndex = Stack.Last();
		Stack.RemoveLast();

		const FBox& LocalBounds = Nodes[nodeIndex].LocalBounds;
		const FVector LocalCenter = (LocalBounds.Min + LocalBounds.Max) * 0.5f;
		const FVector LocalExtent = (LocalBounds.Max - LocalBounds.Min) * 0.5f;

		// 노드마다 바뀌는 건 중심뿐이다. 월드로 한 번 옮긴다
		const FVector4 C = FVector4(LocalCenter, 1.0f) * LocalToWorld;
		const FVector WorldCenter(C.X, C.Y, C.Z);

		// SAT: 한 축이라도 분리되면 겹치지 않는다
		bool bSeparated = false;
		for (int32 k = 0; k < Axes.Count; k++) {
			const float NodeRadius = LocalExtent | Axes.NodeScaledDot[k];
			const float Distance = std::abs((WorldCenter | Axes.Axis[k]) - Axes.DecalCenter[k]);
			if (Distance > Axes.DecalRadius[k] + NodeRadius) {
				bSeparated = true;
				break;
			}
		}
		if (bSeparated) continue;				// 겹치지 않으면 Pruning
		if (Nodes[nodeIndex].IsLeaf()) return true;		// 찾음

		// 더 내려가기
		Stack.Add(Nodes[nodeIndex].Left);
		Stack.Add(Nodes[nodeIndex].Right);
	}

	return false;
}

int32 FOBBTree::BuildNode(uint32 First, uint32 Count, uint32 Depth)
{
	FBox Bounds = TriangleBounds[Triangles[First]];
	FOBBTreeNode Node;
	// 범위 삼각형 전부 감싸는 상자 제작
	for (uint32 i = First + 1; i < First + Count; i++) {
		Bounds.Expand(TriangleBounds[Triangles[i]]);
	}
	Node.LocalBounds = Bounds;

	Node.FirstTriangle = First;
	Node.TriangleCount = Count;

	const int32 NodeIndex = static_cast <int32>(Nodes.Num());
	Nodes.Add(Node);

	// 기저 조건
	if (Depth >= MaxDepth || Count <= MinTriangles) return NodeIndex;


	// 반으로 쪼개기
	const FVector Size = Bounds.Max - Bounds.Min;
	int32 axisIndex = Size.X > Size.Y ? 0 : 1;
	if (Size.Z > Size[axisIndex]) axisIndex = 2;

	const float Mid = (Bounds.Min[axisIndex] + Bounds.Max[axisIndex]) * 0.5f;
	auto Begin = Triangles.begin() + First;
	auto End = Begin + Count;
	auto Split = std::partition(Begin, End, 
		[&](uint32 TriangleIndex) { return Centroids[TriangleIndex][axisIndex] < Mid; });

	// 쏠림 확인 
	uint32 LeftCount = static_cast <uint32> (Split - Begin);
	if (LeftCount == 0 || LeftCount == Count) {
		LeftCount = Count / 2;
	}

	// 재귀
	const int32 Left = BuildNode(First, LeftCount, Depth + 1);
	const int32 Right = BuildNode(First + LeftCount, Count - LeftCount, Depth + 1);

	Nodes[NodeIndex].Left = Left;
	Nodes[NodeIndex].Right = Right;

	return NodeIndex;
}
