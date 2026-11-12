// Clustered(타일 × 깊이 슬라이스)와 Tiled(슬라이스 1개) 라이트 컬링.
// 그룹 하나 = 셀 하나. 그룹 안 스레드들이 라이트를 나눠서 검사한다.
#include "LightCullingCommon.hlsli"

#define THREADS_PER_CELL 64

// 슬라이스 경계의 View 깊이. 원근은 로그 분포, 직교는 선형
float SliceBoundary(uint Slice)
{
    const float NearZ = GridDepthParams.x;
    const float FarZ = GridDepthParams.y;
    const float t = (float) Slice / GridSliceCount;
    return GridIsPerspective == 0 ? lerp(NearZ, FarZ, t) : NearZ * pow(abs(FarZ / NearZ), t);
}

[numthreads(THREADS_PER_CELL, 1, 1)]
void mainCS(uint3 GroupID : SV_GroupID, uint GroupIndex : SV_GroupIndex)
{
    // 그룹 내에서 공유되는 공용 카운터는 한 스레드만 초기화
    // 스레드 간 순서가 보장되지 않지만 Barrier를 통해 0으로 초기화가 끝날때까지 대기한다.
    if (GroupIndex == 0)
    {
        sPointCount = 0;
        sSpotCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const FCullCell Cell = MakeCullCell(GroupID.xy, SliceBoundary(GroupID.z), SliceBoundary(GroupID.z + 1));

    // 스레드 64개가 라이트를 나눠 검사한다. 0번 스레드는 0, 64, 128... 번 라이트를 맡는다.
    float3 CenterVS;
    float CullRadius;
    for (uint i = GroupIndex; i < GridPointCount; i += THREADS_PER_CELL)
    {
        if (LightIntersectsCell(SceneLights[GridLightOffsets.x + i], Cell, CenterVS, CullRadius))
            AppendPointLight(i);
    }
    for (uint i = GroupIndex; i < GridSpotCount; i += THREADS_PER_CELL)
    {
        if (LightIntersectsCell(SceneLights[GridLightOffsets.y + i], Cell, CenterVS, CullRadius))
            AppendSpotLight(i);
    }
    GroupMemoryBarrierWithGroupSync();

    // 셀 번호 = (슬라이스, 행, 열) 순서. PS의 GetLightGridCellIndex와 같은 식이어야 한다
    const uint CellIndex = GetLightGridCellIndexFromSlice(GroupID.xy, GroupID.z);
    WriteCellList(CellIndex, GroupIndex, THREADS_PER_CELL);
}
