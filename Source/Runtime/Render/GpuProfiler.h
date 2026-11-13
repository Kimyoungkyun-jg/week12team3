#pragma once

#include "Core/Types.h"
#include "Render/RenderDevice.h"

#include <array>
#include <algorithm>
#include <memory>
#include <vector>
#include <wrl/client.h>

// GPU 시간은 CPU 사이클이 아니므로 ms로 직접 저장한다.
struct FGpuTimingHistory
{
    static constexpr uint32 Capacity = 120;

    std::array<double, Capacity> Samples{};
    uint32 Next = 0;
    uint32 Count = 0;

    double LastMs = 0.0;
    double SumMs = 0.0;
    uint64 LastFrameId = 0;
    uint64 LastVSInvocations = 0;

    void Push(double Ms, uint64 FrameId)
    {
        SumMs -= Samples[Next];
        Samples[Next] = Ms;
        SumMs += Ms;

        Next = (Next + 1) % Capacity;
        Count = std::min(Count + 1, Capacity);

        LastMs = Ms;
        LastFrameId = FrameId;
        LastVSInvocations = 0;
    }

    void AccumulateDraws(double Ms, uint64 VSInvocations, uint64 FrameId)
    {
        if (Count == 0 || LastFrameId != FrameId)
            Push(Ms, FrameId);
        else
        {
            Samples[(Next + Capacity - 1) % Capacity] += Ms;
            SumMs += Ms;
            LastMs += Ms;
        }
        LastVSInvocations += VSInvocations;
    }

    double GetAverageMs() const
    {
        return Count ? SumMs / Count : 0.0;
    }

    double GetMaxMs() const
    {
        double Result = 0.0;
        for (uint32 Index = 0; Index < Count; ++Index)
        {
            Result = std::max(Result, Samples[Index]);
        }
        return Result;
    }
};

class FGpuProfiler
{
public:
    enum class EScopeKind { Pass, SkeletalDraw, GpuSkinnedDraw, SkinningDispatch };

    static FGpuProfiler& Get()
    {
        static FGpuProfiler Instance;
        return Instance;
    }

    int32 BeginSkinningDispatch()
    {
        if (ActiveSlot < 0)
            return -1;
        if (!SkinningDispatchTiming)
            SkinningDispatchTiming = std::make_shared<FGpuTimingHistory>();
        return BeginScope(SkinningDispatchTiming, EScopeKind::SkinningDispatch);
    }

    const FGpuTimingHistory* GetSkinningDispatchTiming() const { return SkinningDispatchTiming.get(); }

    // 렌더링할 엔진 프레임마다 한 번 호출한다.
    void BeginFrame(bool bEnabled)
    {
        // 이전 프레임에서 EndFrame을 빠뜨리는 사용 오류 방지.
        if (ActiveSlot >= 0)
            return;

        ++FrameId;

        FRenderDevice* Device = FRenderDevice::Get();
        if (!Device)
            return;

        ID3D11DeviceContext* Context = Device->GetContext();

        // stat을 껐어도 이미 발행한 결과는 회수한다.
        Poll(Context);

        if (!bEnabled)
            return;

        for (uint32 Offset = 0; Offset < FrameSlotCount; ++Offset)
        {
            const uint32 Index =
                (NextSlot + Offset) % FrameSlotCount;

            FFrameSlot& Slot = Slots[Index];
            if (Slot.bPending)
                continue;

            if (!Slot.Disjoint)
            {
                D3D11_QUERY_DESC Desc{};
                Desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

                if (FAILED(Device->GetDevice()->CreateQuery(
                    &Desc,
                    Slot.Disjoint.ReleaseAndGetAddressOf())))
                {
                    ++ErrorCount;
                    return;
                }
            }

            Slot.FrameId = FrameId;
            Slot.ScopeCount = 0;
            Slot.IncompleteDrawTimings.clear();
            Slot.bDiscard = false;

            ActiveSlot = static_cast<int32>(Index);
            NextSlot = (Index + 1) % FrameSlotCount;

            Context->Begin(Slot.Disjoint.Get());
            return;
        }

        // 전부 GPU 처리 중이면 기다리지 않고 이번 측정을 건너뛴다.
        ++SkippedFrames;
    }

    // 패스 또는 연속된 스켈레탈 드로우의 시작. 반환값은 측정 구간의 핸들이다.
    int32 BeginScope(
        const std::shared_ptr<FGpuTimingHistory>& Destination,
        EScopeKind Kind = EScopeKind::Pass)
    {
        if (ActiveSlot < 0 || !Destination)
            return -1;

        FRenderDevice* Device = FRenderDevice::Get();
        if (!Device)
            return -1;

        FFrameSlot& Slot = Slots[ActiveSlot];

        const bool bSkeletalDraw = Kind != EScopeKind::Pass;
        if (bSkeletalDraw && Slot.IsDrawTimingIncomplete(Destination))
            return -1;

        // Keep capacity for existing pass timers even when draw profiling fills its budget.
        const uint32 ScopeLimit = bSkeletalDraw ? MaxScopesPerFrame - ReservedPassScopes : MaxScopesPerFrame;
        if (Slot.ScopeCount >= ScopeLimit)
        {
            if (bSkeletalDraw)
                Slot.IncompleteDrawTimings.push_back(Destination);
            ++SkippedScopes;
            return -1;
        }

        FScope& Scope = Slot.Scopes[Slot.ScopeCount];

        D3D11_QUERY_DESC Desc{};
        Desc.Query = D3D11_QUERY_TIMESTAMP;

        if (!Scope.Start)
        {
            if (FAILED(Device->GetDevice()->CreateQuery(
                &Desc,
                Scope.Start.ReleaseAndGetAddressOf())))
            {
                if (bSkeletalDraw)
                    Slot.IncompleteDrawTimings.push_back(Destination);
                ++ErrorCount;
                return -1;
            }
        }

        if (!Scope.End)
        {
            if (FAILED(Device->GetDevice()->CreateQuery(
                &Desc,
                Scope.End.ReleaseAndGetAddressOf())))
            {
                if (bSkeletalDraw)
                    Slot.IncompleteDrawTimings.push_back(Destination);
                ++ErrorCount;
                return -1;
            }
        }

        const bool bCountVSInvocations = Kind == EScopeKind::GpuSkinnedDraw;
        if (bCountVSInvocations && !Scope.PipelineStatistics)
        {
            Desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
            if (FAILED(Device->GetDevice()->CreateQuery(&Desc, Scope.PipelineStatistics.ReleaseAndGetAddressOf())))
            {
                Slot.IncompleteDrawTimings.push_back(Destination);
                ++ErrorCount;
                return -1;
            }
        }

        Scope.Destination = Destination;
        Scope.Kind = Kind;
        Scope.bEnded = false;

        const int32 Handle =
            static_cast<int32>(Slot.ScopeCount++);

        ID3D11DeviceContext* Context = Device->GetContext();
        if (bCountVSInvocations)
            Context->Begin(Scope.PipelineStatistics.Get());
        // Timestamp 쿼리는 Begin이 아니라 End로 찍는다.
        Context->End(Scope.Start.Get());
        return Handle;
    }

    void EndScope(int32 Handle)
    {
        if (ActiveSlot < 0 || Handle < 0)
            return;

        FFrameSlot& Slot = Slots[ActiveSlot];

        if (static_cast<uint32>(Handle) >= Slot.ScopeCount)
            return;

        FScope& Scope = Slot.Scopes[Handle];
        if (Scope.bEnded)
            return;

        ID3D11DeviceContext* Context = FRenderDevice::Get()->GetContext();
        Context->End(Scope.End.Get());
        if (Scope.Kind == EScopeKind::GpuSkinnedDraw)
            Context->End(Scope.PipelineStatistics.Get());
        Scope.bEnded = true;
    }

    void EndFrame()
    {
        if (ActiveSlot < 0)
            return;

        FFrameSlot& Slot = Slots[ActiveSlot];
        ID3D11DeviceContext* Context =
            FRenderDevice::Get()->GetContext();

        // 정상 코드에서는 모든 scope가 닫혀 있어야 한다.
        // 누락되면 쿼리는 닫되 이번 프레임 결과를 버린다.
        for (uint32 Index = 0; Index < Slot.ScopeCount; ++Index)
        {
            FScope& Scope = Slot.Scopes[Index];

            if (!Scope.bEnded)
            {
                EndScope(static_cast<int32>(Index));
                Slot.bDiscard = true;
            }
        }

        Context->End(Slot.Disjoint.Get());
        Slot.bPending = true;
        ActiveSlot = -1;
    }

    // RenderDevice 해제 전에 호출한다.
    void Shutdown()
    {
        EndFrame();

        for (FFrameSlot& Slot : Slots)
            Slot = FFrameSlot{};

        ActiveSlot = -1;
        NextSlot = 0;
        SkinningDispatchTiming.reset();
    }

    uint64 GetFrameId() const { return FrameId; }
    uint64 GetSkippedFrames() const { return SkippedFrames; }
    uint64 GetSkippedScopes() const { return SkippedScopes; }
    uint64 GetErrorCount() const { return ErrorCount; }

private:
    static constexpr uint32 FrameSlotCount = 4;

    static constexpr uint32 ReservedPassScopes = 32;
    static constexpr uint32 MaxScopesPerFrame = ReservedPassScopes + 256;

    using FQueryPtr =
        Microsoft::WRL::ComPtr<ID3D11Query>;

    struct FScope
    {
        FQueryPtr Start;
        FQueryPtr End;
        FQueryPtr PipelineStatistics;

        // 뷰가 사라지거나 Reset되면 결과를 기록하지 않는다.
        std::weak_ptr<FGpuTimingHistory> Destination;
        EScopeKind Kind = EScopeKind::Pass;
        bool bEnded = false;
    };

    struct FFrameSlot
    {
        FQueryPtr Disjoint;
        std::array<FScope, MaxScopesPerFrame> Scopes{};
        std::vector<std::shared_ptr<FGpuTimingHistory>> IncompleteDrawTimings;

        uint64 FrameId = 0;
        uint32 ScopeCount = 0;

        bool bPending = false;
        bool bDiscard = false;

        bool IsDrawTimingIncomplete(const std::shared_ptr<FGpuTimingHistory>& Destination) const
        {
            return std::find(IncompleteDrawTimings.begin(), IncompleteDrawTimings.end(), Destination)
                != IncompleteDrawTimings.end();
        }
    };

    void Poll(ID3D11DeviceContext* Context)
    {
        // 오래된 프레임부터 처리해 Last 값이 역행하지 않게 한다.
        for (;;)
        {
            int32 Oldest = -1;

            for (uint32 Index = 0; Index < FrameSlotCount; ++Index)
            {
                if (!Slots[Index].bPending)
                    continue;

                if (Oldest < 0 ||
                    Slots[Index].FrameId < Slots[Oldest].FrameId)
                {
                    Oldest = static_cast<int32>(Index);
                }
            }

            if (Oldest < 0)
                return;

            if (!TryResolve(Context, Slots[Oldest]))
                return;
        }
    }

    // true: 완료 또는 오류로 슬롯을 회수함
    // false: 아직 GPU 처리 중
    bool TryResolve(
        ID3D11DeviceContext* Context,
        FFrameSlot& Slot)
    {
        constexpr UINT Flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT Disjoint{};

        const HRESULT DisjointResult = Context->GetData(
            Slot.Disjoint.Get(),
            &Disjoint,
            sizeof(Disjoint),
            Flags);

        if (DisjointResult == S_FALSE)
            return false;

        if (FAILED(DisjointResult))
        {
            ++ErrorCount;
            Slot.bPending = false;
            return true;
        }

        std::array<UINT64, MaxScopesPerFrame> Starts{};
        std::array<UINT64, MaxScopesPerFrame> Ends{};
        std::array<UINT64, MaxScopesPerFrame> VSInvocations{};

        // 전체 결과가 준비되기 전에는 아무 것도 Push하지 않는다.
        // 일부만 Push하면 다음 Poll에서 중복 기록될 수 있다.
        for (uint32 Index = 0; Index < Slot.ScopeCount; ++Index)
        {
            FScope& Scope = Slot.Scopes[Index];

            const HRESULT StartResult = Context->GetData(
                Scope.Start.Get(),
                &Starts[Index],
                sizeof(UINT64),
                Flags);

            const HRESULT EndResult = Context->GetData(
                Scope.End.Get(),
                &Ends[Index],
                sizeof(UINT64),
                Flags);

            if (FAILED(StartResult) || FAILED(EndResult))
            {
                ++ErrorCount;
                Slot.bPending = false;
                return true;
            }

            if (StartResult == S_FALSE || EndResult == S_FALSE)
                return false;
            if (Scope.Kind == EScopeKind::GpuSkinnedDraw)
            {
                D3D11_QUERY_DATA_PIPELINE_STATISTICS Statistics{};
                const HRESULT Result = Context->GetData(
                    Scope.PipelineStatistics.Get(), &Statistics, sizeof(Statistics), Flags);
                if (Result == S_FALSE)
                    return false;
                if (FAILED(Result))
                {
                    ++ErrorCount;
                    Slot.bPending = false;
                    return true;
                }
                VSInvocations[Index] = Statistics.VSInvocations;
            }
        }

        if (!Slot.bDiscard &&
            !Disjoint.Disjoint &&
            Disjoint.Frequency != 0)
        {
            for (uint32 Index = 0; Index < Slot.ScopeCount; ++Index)
            {
                if (Ends[Index] < Starts[Index] && Slot.Scopes[Index].Kind != EScopeKind::Pass)
                {
                    if (auto Destination = Slot.Scopes[Index].Destination.lock())
                        Slot.IncompleteDrawTimings.push_back(Destination);
                }
            }
            for (uint32 Index = 0; Index < Slot.ScopeCount; ++Index)
            {
                if (Ends[Index] < Starts[Index])
                    continue;

                if (auto Destination =
                    Slot.Scopes[Index].Destination.lock())
                {
                    const double Ms =
                        static_cast<double>(
                            Ends[Index] - Starts[Index]) *
                        1000.0 /
                        static_cast<double>(Disjoint.Frequency);

                    if (Slot.Scopes[Index].Kind == EScopeKind::Pass)
                        Destination->Push(Ms, Slot.FrameId);
                    else if (!Slot.IsDrawTimingIncomplete(Destination))
                        Destination->AccumulateDraws(Ms, VSInvocations[Index], Slot.FrameId);
                }
            }
        }

        Slot.bPending = false;
        return true;
    }

    std::array<FFrameSlot, FrameSlotCount> Slots{};
    std::shared_ptr<FGpuTimingHistory> SkinningDispatchTiming;

    int32 ActiveSlot = -1;
    uint32 NextSlot = 0;

    uint64 FrameId = 0;
    uint64 SkippedFrames = 0;
    uint64 SkippedScopes = 0;
    uint64 ErrorCount = 0;
};
