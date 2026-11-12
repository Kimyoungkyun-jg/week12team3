#include "EnginePCH.h"
#include "Render/RealtimeGPUProfiler.h"
#include <cstring>
#include <string>

namespace
{
    // 스탯 이름은 ASCII라 바이트를 그대로 넓힌다. 한글 이름을 쓰면 MultiByteToWideChar로 바꿔야 한다
    std::wstring ToWide(const char* Text)
    {
        return std::wstring(Text, Text + std::strlen(Text));
    }
}

ID3D11DeviceContext* FRealtimeGPUProfiler::Context()
{
    FRenderDevice* Device = FRenderDevice::Get();
    return Device ? Device->GetContext() : nullptr;
}

ID3DUserDefinedAnnotation* FRealtimeGPUProfiler::Annotation()
{
    if (!bAnnotationQueried)
    {
        bAnnotationQueried = true;
        if (ID3D11DeviceContext* Ctx = Context())
            Ctx->QueryInterface(IID_PPV_ARGS(AnnotationInterface.GetAddressOf()));   // 실패하면 null로 남는다
    }
    return AnnotationInterface.Get();
}

bool FRealtimeGPUProfiler::EnsureQueries()
{
    if (bCreated) return true;
    if (bCreateFailed) return false;

    FRenderDevice* RenderDevice = FRenderDevice::Get();
    ID3D11Device* Device = RenderDevice ? RenderDevice->GetDevice() : nullptr;
    if (!Device || !Context()) { bCreateFailed = true; return false; }

    const D3D11_QUERY_DESC DisjointDesc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
    const D3D11_QUERY_DESC TimestampDesc{ D3D11_QUERY_TIMESTAMP, 0 };
    bool bOk = true;
    for (FFrame& F : Frames)
    {
        bOk = bOk && SUCCEEDED(Device->CreateQuery(&DisjointDesc, F.Disjoint.GetAddressOf()));
        for (ComPtr<ID3D11Query>& Q : F.Timestamps)
            bOk = bOk && SUCCEEDED(Device->CreateQuery(&TimestampDesc, Q.GetAddressOf()));
    }
    bCreated = bOk;
    bCreateFailed = !bOk;
    return bOk;
}

void FRealtimeGPUProfiler::Shutdown()
{
    for (FFrame& F : Frames)
    {
        F.Disjoint.Reset();
        for (ComPtr<ID3D11Query>& Q : F.Timestamps) Q.Reset();
        F.Events.Reset();
        F.NumUsed = 0;
        F.bPending = F.bActive = false;
    }
    OpenStack.Reset();
    AnnotationInterface.Reset();
    bAnnotationQueried = bCreated = bCreateFailed = false;
}

void FRealtimeGPUProfiler::BeginFrame()
{
    if (!FStatRegistry::IsEnabled() || !EnsureQueries()) return;   // 스탯을 끄면 쿼리를 아예 내지 않는다
    FFrame& F = Frames[FrameIndex];
    if (F.bPending) Resolve(F);                 // FramesInFlight 프레임 전에 쓴 슬롯
    F.Events.Reset();
    F.NumUsed = 0;
    OpenStack.Reset();
    Context()->Begin(F.Disjoint.Get());
    F.bActive = true;
}

void FRealtimeGPUProfiler::PushScope(TStatId Id)
{
    FFrame& F = Frames[FrameIndex];
    if (!F.bActive || F.NumUsed + 2 > MaxTimestamps) { OpenStack.Add(-1); return; }   // 꺼짐 또는 풀 소진
    Context()->End(F.Timestamps[F.NumUsed].Get());
    OpenStack.Add(F.Events.Num());
    F.Events.Add({ Id, static_cast<uint16>(F.NumUsed++), 0 });
    if (Annotation()) Annotation()->BeginEvent(ToWide(Id.GetName()).c_str());   // RenderDoc/PIX에도 같은 이름
}

void FRealtimeGPUProfiler::PopScope()
{
    if (OpenStack.IsEmpty()) return;               // 짝이 안 맞는 Pop 방어
    const int32 EventIndex = OpenStack.Last();
    OpenStack.Pop();                               // 이 엔진의 TArray::Pop()은 void (Array.h:261)
    if (EventIndex < 0) return;
    FFrame& F = Frames[FrameIndex];
    Context()->End(F.Timestamps[F.NumUsed].Get());
    F.Events[EventIndex].End = static_cast<uint16>(F.NumUsed++);
    if (Annotation()) Annotation()->EndEvent();
}

void FRealtimeGPUProfiler::EndFrame()
{
    FFrame& F = Frames[FrameIndex];
    if (!F.bActive) return;
    Context()->End(F.Disjoint.Get());
    F.bActive = false; F.bPending = true;
    FrameIndex = (FrameIndex + 1) % FramesInFlight;
}

void FRealtimeGPUProfiler::Resolve(FFrame& F)
{
    F.bPending = false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT D{};
    if (Context()->GetData(F.Disjoint.Get(), &D, sizeof(D), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
        || D.Disjoint || D.Frequency == 0) {
        ++DroppedFrames; return;
    }
    for (const FEvent& E : F.Events)
    {
        if (E.End == 0) continue;                  // 프레임 안에서 닫히지 않은 스코프
        UINT64 T0 = 0, T1 = 0;
        if (Context()->GetData(F.Timestamps[E.Begin].Get(), &T0, sizeof(T0), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (Context()->GetData(F.Timestamps[E.End].Get(), &T1, sizeof(T1), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (T1 < T0) continue;
        FStatRegistry::AddGPUSeconds(E.Id, double(T1 - T0) / double(D.Frequency));   // 같은 이름은 합산
    }
    FStatRegistry::EndGPUFrame();
}