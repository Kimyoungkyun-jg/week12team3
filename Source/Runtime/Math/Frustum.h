#pragma once

#include "Box.h"

struct FPlane { FVector Normal; float Distance; };
struct FFrustumPlanes { FPlane Planes[6]; };
struct FAABB { FVector Center; FVector Extent; };
struct FOBB {
	FVector Center;
	FVector Axis[3];
	FVector Extent;
};

struct FFrustumCorners
{
	FVector Corners[8];

	FVector& NTL() { return Corners[0]; }
	const FVector& NTL() const { return Corners[0]; }

	FVector& NTR() { return Corners[1]; }
	const FVector& NTR() const { return Corners[1]; }

	FVector& NBL() { return Corners[2]; }
	const FVector& NBL() const { return Corners[2]; }

	FVector& NBR() { return Corners[3]; }
	const FVector& NBR() const { return Corners[3]; }

	FVector& FTL() { return Corners[4]; }
	const FVector& FTL() const { return Corners[4]; }

	FVector& FTR() { return Corners[5]; }
	const FVector& FTR() const { return Corners[5]; }

	FVector& FBL() { return Corners[6]; }
	const FVector& FBL() const { return Corners[6]; }

	FVector& FBR() { return Corners[7]; }
	const FVector& FBR() const { return Corners[7]; }
};

static FPlane NormalizePlane(const FPlane Plane);
FFrustumPlanes ExtractFrustumPlanes(const FMatrix& Matrix);
FFrustumCorners GenerateFrustumCorners(const FMatrix& InvViewProjection);
bool IsAABBInFrustum(const FAABB& Bounds, const FFrustumPlanes& Frustum);
FAABB MakeWorldBounds(const FBox& Value);

inline bool IntersectAABB(const FAABB& A, const FAABB& B) {
	if (std::abs(A.Center.X - B.Center.X) > A.Extent.X + B.Extent.X) return false;
	if (std::abs(A.Center.Y - B.Center.Y) > A.Extent.Y + B.Extent.Y) return false;
	if (std::abs(A.Center.Z - B.Center.Z) > A.Extent.Z + B.Extent.Z) return false;
	return true;
}

inline float GetProjectedRadius(const FOBB& Box, const FVector& L) {
	return std::abs(Box.Extent.X * (Box.Axis[0] | L)) +
		std::abs(Box.Extent.Y * (Box.Axis[1] | L)) +
		std::abs(Box.Extent.Z * (Box.Axis[2] | L));
}

inline bool IsSeparated(const FVector& L, const FOBB& A, const FAABB& B) {
	// L : 분리축, A : OBB, B : AABB (SAT)
	const FVector D = B.Center - A.Center;
	const float d = std::abs(D | L);	// | : 내적

	const float rA = GetProjectedRadius(A, L);
	const float rB = std::abs(B.Extent.X * L.X) +
					std::abs(B.Extent.Y * L.Y) +
					std::abs(B.Extent.Z * L.Z);
	return d > rA + rB;		// 분리되어 있다! (만나지 않는다)
}

inline bool IsSeparated(const FVector& L, const FOBB& A, const FOBB& B) {
	// L : 분리축, A : OBB, B : OBB (SAT)
	const FVector D = B.Center - A.Center;
	const float d = std::abs(D | L);	// | : 내적

	const float rA = GetProjectedRadius(A, L);
	const float rB = GetProjectedRadius(B, L);
	return d > rA + rB;		// 분리되어 있다! (만나지 않는다)
}

inline bool IntersectOBBAABB(const FOBB& A, const FAABB& B) {
	const FVector WorldAxis[3] = { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1) };

	// OBB vs AABB 분리축 테스트!

	// 면 법선 6축 검사
	for (int32 i = 0; i < 3; ++i) {
		if (IsSeparated(A.Axis[i], A, B)) return false;
		if (IsSeparated(WorldAxis[i], A, B)) return false;
	}

	// 모서리 외적 9축 검사
	for (int32 i = 0; i < 3; i++) {
		for (int32 j = 0; j < 3; j++) {
			const FVector L = A.Axis[i] ^ WorldAxis[j];	// ^ : 외적
			if (L.SizeSquared() < 1e-6f) continue;	// 평행하면 분리 못찾음
			if (IsSeparated(L, A, B)) return false;
		}
	}

	return true;
}

inline bool IntersectOBBOBB(const FOBB& A, const FOBB& B) {
	// OBB vs OBB 분리축 테스트

	// 면 법선 6축 검사
	for (int32 i = 0; i < 3; ++i) {
		if (IsSeparated(A.Axis[i], A, B)) return false;
		if (IsSeparated(B.Axis[i], A, B)) return false;
	}

	// 모서리 외적 9축 검사
	for (int32 i = 0; i < 3; i++) {
		for (int32 j = 0; j < 3; j++) {
			const FVector L = A.Axis[i] ^ B.Axis[j];	// ^ : 외적
			if (L.SizeSquared() < 1e-6f) continue;	// 평행하면 분리 못찾음
			if (IsSeparated(L, A, B)) return false;
		}
	}

	return true;
}

enum class EFrustumTest : uint8 { Outside, Intersect, Inside };

// 6개 평면 모두 검사해야 하는 초기 마스크. 비트 i가 켜져 있으면 평면 i를 아직 검사해야 한다.
inline constexpr uint32 FrustumAllPlanesMask = 0x3F;

// Min/Max 박스를 변환 없이 바로 판정한다. 박스가 완전히 안쪽에 있는 평면은 마스크에서 지워
// 자식 노드가 그 평면을 다시 검사하지 않게 한다. 마스크가 0이 되면 Inside다.
inline EFrustumTest ClassifyBoxInFrustum(const FBox& Box, const FFrustumPlanes& Frustum, uint32& InOutPlaneMask)
{
	const float CX = (Box.Min.X + Box.Max.X) * 0.5f;
	const float CY = (Box.Min.Y + Box.Max.Y) * 0.5f;
	const float CZ = (Box.Min.Z + Box.Max.Z) * 0.5f;
	const float EX = (Box.Max.X - Box.Min.X) * 0.5f;
	const float EY = (Box.Max.Y - Box.Min.Y) * 0.5f;
	const float EZ = (Box.Max.Z - Box.Min.Z) * 0.5f;

	for (uint32 PlaneIndex = 0; PlaneIndex < 6; ++PlaneIndex)
	{
		const uint32 Bit = 1u << PlaneIndex;
		if (!(InOutPlaneMask & Bit))
			continue;

		const FPlane& Plane = Frustum.Planes[PlaneIndex];
		const float Distance = Plane.Normal.X * CX + Plane.Normal.Y * CY + Plane.Normal.Z * CZ + Plane.Distance;
		const float Radius = std::abs(Plane.Normal.X) * EX + std::abs(Plane.Normal.Y) * EY + std::abs(Plane.Normal.Z) * EZ;

		if (Distance + Radius < 0.0f)
			return EFrustumTest::Outside;
		if (Distance - Radius >= 0.0f)
			InOutPlaneMask &= ~Bit;
	}

	return InOutPlaneMask == 0 ? EFrustumTest::Inside : EFrustumTest::Intersect;
}