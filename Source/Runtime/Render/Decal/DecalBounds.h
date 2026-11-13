#pragma once

#include "Render/Decal/ProjectedDecal.h"
#include "Math/Box.h"
#include "Math/Frustum.h"
#include "Engine/PrimitiveSceneProxy.h"

#include <cmath>

namespace DecalBounds
{
	inline FBox GetLocalBox()
	{
		return FBox{
			FVector(-0.5f, -0.5f, -0.5f),
			FVector(0.5f,  0.5f,  0.5f)
		};
	}

	inline FBox GetWorldBox(
		const FProjectedDecalRenderData& Decal)
	{
		return GetLocalBox().GetWorldAABB(Decal.DecalToWorld);
	}

    inline bool IsFinite(const FBox& Box)
    {
        return
            std::isfinite(Box.Min.X) &&
            std::isfinite(Box.Min.Y) &&
            std::isfinite(Box.Min.Z) &&
            std::isfinite(Box.Max.X) &&
            std::isfinite(Box.Max.Y) &&
            std::isfinite(Box.Max.Z) &&
            Box.Min.X <= Box.Max.X &&
            Box.Min.Y <= Box.Max.Y &&
            Box.Min.Z <= Box.Max.Z;
    }

    inline bool Overlaps(
        const FBox& A,
        const FBox& B,
        float Tolerance = 0.0f)
    {
        return
            A.Min.X <= B.Max.X + Tolerance &&
            A.Max.X >= B.Min.X - Tolerance &&
            A.Min.Y <= B.Max.Y + Tolerance &&
            A.Max.Y >= B.Min.Y - Tolerance &&
            A.Min.Z <= B.Max.Z + Tolerance &&
            A.Max.Z >= B.Min.Z - Tolerance;
    }

    inline FBox Expand(const FBox& Box, float Amount)
    {
        const FVector Padding(Amount, Amount, Amount);

        return FBox{
            Box.Min - Padding,
            Box.Max + Padding
        };
    }

    inline FBox GetProxyWorldBox(
        const FPrimitiveSceneProxy& Proxy)
    {
        const FAABB& Bounds = Proxy.GetBounds();

        return FBox{
            Bounds.Center - Bounds.Extent,
            Bounds.Center + Bounds.Extent
        };
    }

    // 추가적인 보수적 검사.
    // 메시 월드 AABB를 데칼 로컬 공간으로 변환해
    // 로컬 단위 박스와 겹치는지 확인한다.
    inline bool MayIntersectVolume(
        const FBox& ReceiverWorldBox,
        const FMatrix& WorldToDecal)
    {
        const FBox ReceiverInDecal =
            ReceiverWorldBox.GetWorldAABB(WorldToDecal);

        // 수치 문제가 있으면 잘못 제거하지 않고 통과시킨다.
        if (!IsFinite(ReceiverInDecal))
            return true;

        return Overlaps(
            GetLocalBox(),
            ReceiverInDecal,
            1e-4f);
    }
}