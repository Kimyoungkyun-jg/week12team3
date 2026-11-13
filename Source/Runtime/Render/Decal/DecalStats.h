#pragma once

#include "Core/Stats/LightweightStats.h"
#include "Render/RenderEnums.h"
#include "Render/GpuProfiler.h"

#include <algorithm>
#include <memory>
#include <optional>

struct FDecalFrameStats
{
    bool bValid = false;
    bool bHasDBufferPasses = false;

    EDecalRenderMethod Method =
        EDecalRenderMethod::MeshRerendering;

    uint32 Width = 0;
    uint32 Height = 0;

    uint32 WorldCount = 0;
    uint32 HiddenCount = 0;

    // 비활성, 재질/텍스처 없음, 잘못된 값 등을 포함.
    uint32 RenderDataRejected = 0;
	uint32 EligibleCount = 0;               // Frustum culling 이전에 RenderData가 유효한 데칼 수

    uint32 SubmittedLit = 0;
    uint32 SubmittedUnlit = 0;

    uint32 ReceiverBatchCount = 0;
    uint32 ReceiverItemCount = 0;

    uint32 DrawCalls = 0;
    uint64 SubmittedTriangles = 0;

    uint64 GatherCycles = 0;
    uint64 SortCycles = 0;
    uint64 SubmitCycles = 0;
    uint64 ReceiverSelectionCycles = 0;

    uint64 DBufferBytes = 0;

    uint32 VolumeDrawCalls = 0;
    uint32 FarFallbackDrawCalls = 0;
    uint32 FrustumRejectedDecals = 0;
    uint32 InvalidGeometryDecals = 0;

    uint32 ViewFrustumRejectedDecals = 0;   // 공통 수집 단계에서 제외한 데칼 수
    uint64 MeshBVHBoundsTests = 0;          // BVH의 노드와 원소 Bounds 판정 횟수 합계
    uint64 MeshBVHProxyCandidates = 0;      // BVH 조회에서 반환된 프록시 수. 현재 뷰의 수신 대상이 아닌 프록시도 포함.
    uint64 MeshRefineRejectedProxies = 0;   // 추가 로컬 공간 검사에서 제외한 프록시 수
    uint64 MeshMatchedItems = 0;            // 최종 선택된 데칼-Item 조합 수
    uint64 MeshFallbackItems = 0;           // BVH에 연결할 수 없어 보수적으로 포함한 조합 수.
    uint32 MeshNoReceiverDecals = 0;
};

struct FDecalStats
{
    FDecalFrameStats Frame;

    FCycleStatData GatherHistory;
    FCycleStatData SortHistory;
    FCycleStatData SubmitHistory;

    // Proxy->Item 맵 생성 + BVH 조회 + 추가 교차 검사 + 결과 정렬
    FCycleStatData ReceiverSelectionHistory;

    std::shared_ptr<FGpuTimingHistory> GpuLit;
    std::shared_ptr<FGpuTimingHistory> GpuUnlit;

    bool bCollecting = false;

    void ResetHistory()
    {
        GatherHistory = {};
        SortHistory = {};
        SubmitHistory = {};
		ReceiverSelectionHistory = {};

        // 이전 GPU 쿼리는 weak_ptr만 갖고 있으므로
        // 이전 측정 결과가 새 이력에 섞이지 않는다.
        GpuLit = std::make_shared<FGpuTimingHistory>();
        GpuUnlit = std::make_shared<FGpuTimingHistory>();
    }

    void BeginView(
        bool bEnabled,
        EDecalRenderMethod Method,
        uint32 Width,
        uint32 Height)
    {
        const bool bChanged =
            bCollecting != bEnabled ||
            Frame.Method != Method ||
            Frame.Width != Width ||
            Frame.Height != Height;

        if (bChanged || !GpuLit || !GpuUnlit)
            ResetHistory();

        bCollecting = bEnabled;

        Frame = {};
        Frame.Method = Method;
        Frame.Width = Width;
        Frame.Height = Height;
    }

    void EndView(bool bSuccess)
    {
        Frame.bValid = bCollecting && bSuccess;

        if (!Frame.bValid)
            return;

        // 처리할 데칼이 없어도 0을 Push한다.
        GatherHistory.PushFrame(Frame.GatherCycles);
        SortHistory.PushFrame(Frame.SortCycles);
        SubmitHistory.PushFrame(Frame.SubmitCycles);
		ReceiverSelectionHistory.PushFrame(Frame.ReceiverSelectionCycles);
    }
};

// 기존 타이머를 뷰별 목적지에 기록하는 작은 어댑터.
class FDecalCpuScope
{
public:
    FDecalCpuScope(bool bEnabled, uint64& InDestination)
        : Destination(InDestination)
    {
        if (bEnabled)
        {
            // stat decal의 수집 여부는 여기서 직접 결정한다.
            // 전역 Profile 설정에 의해 꺼지지 않도록 always=true.
            Timer.emplace(TStatId{}, true);
        }
    }

    ~FDecalCpuScope()
    {
        if (Timer)
            Destination += Timer->Finish();
    }

    FDecalCpuScope(const FDecalCpuScope&) = delete;
    FDecalCpuScope& operator=(const FDecalCpuScope&) = delete;

private:
    uint64& Destination;
    std::optional<FScopeCycleCounter> Timer;
};

// 기존 GetMaxMs()는 초기화 이후 최대값.
// 최근 구간 최대값은 기존 프레임 배열에서 계산한다.
inline double GetDecalRecentMaxMs(const FCycleStatData& History)
{
    uint64 MaxCycles = 0;

    for (uint32 Index = 0; Index < History.SampleCount; ++Index)
    {
        MaxCycles = std::max(
            MaxCycles,
            History.FrameCycles[Index]);
    }

    return FPlatformTime::ToMilliseconds64(MaxCycles);
}

class FDecalViewStatsScope
{
public:
    explicit FDecalViewStatsScope(FDecalStats& InStats)
        : Stats(InStats)
    {
    }

    ~FDecalViewStatsScope()
    {
        Stats.EndView(bSuccess);
    }

    void MarkSuccess()
    {
        bSuccess = true;
    }

private:
    FDecalStats& Stats;
    bool bSuccess = false;
};