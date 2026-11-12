#pragma once
#include "Core/Stats/LightweightStats.h"
#include "Render/RenderDevice.h"

#define DECLARE_GPU_STAT(CounterName, StatId) DECLARE_CYCLE_STAT(CounterName, StatId)

class FRealtimeGPUProfiler
{
public:
    static void BeginFrame();                 
    static void EndFrame();                   
    static void PushScope(TStatId Id);
    static void PopScope();
    static void Shutdown();                   
    static uint32 GetDroppedFrames() { return DroppedFrames; }

private:
    static ID3D11DeviceContext* Context();
    static ID3DUserDefinedAnnotation* Annotation();  
    static constexpr uint32 FramesInFlight = 4;  // 최대 프레임 지연(기본 3) + 1
    static constexpr uint32 MaxTimestamps = 256;  // 프레임당 스코프 128개

    struct FEvent { TStatId Id; uint16 Begin = 0; uint16 End = 0; };
    struct FFrame
    {
        ComPtr<ID3D11Query> Disjoint;
        ComPtr<ID3D11Query> Timestamps[MaxTimestamps];
        TArray<FEvent> Events;
        uint32 NumUsed = 0;
        bool   bPending = false;
        bool   bActive = false;
    };
    static bool EnsureQueries();
    static void Resolve(FFrame& Frame);

    inline static FFrame Frames[FramesInFlight];
    inline static uint32 FrameIndex = 0;
    inline static TArray<int32> OpenStack;  
    inline static uint32 DroppedFrames = 0;
    inline static bool bCreated = false, bCreateFailed = false;
    inline static ComPtr<ID3DUserDefinedAnnotation> AnnotationInterface;
    inline static bool bAnnotationQueried = false;
};

struct FScopedGPUStat
{
    explicit FScopedGPUStat(TStatId Id) { FRealtimeGPUProfiler::PushScope(Id); }
    ~FScopedGPUStat() { FRealtimeGPUProfiler::PopScope(); }
    FScopedGPUStat(const FScopedGPUStat&) = delete;
    FScopedGPUStat& operator=(const FScopedGPUStat&) = delete;
};

#define SCOPED_GPU_STAT(StatId) FScopedGPUStat STATS_JOIN(GpuStat_, __LINE__)(GET_STATID(StatId))

