#pragma once

#include "Core/Stats/LightweightStats.h"
#include "Core/StatOverlay.h"
#include "Render/RenderEnums.h"
#include "Render/GpuProfiler.h"

#include <algorithm>
#include <memory>
#include <optional>

// stat skinning: CPU와 GPU 스키닝을 같은 장면에서 바꿔 가며 비교하기 위한 수치.
//  - CPU 스키닝: 본 행렬 계산 + 정점마다 행렬 섞기 + 정점 버퍼 업로드(정점 수 x 48B)
//  - GPU 스키닝: 본 행렬 계산 + 본 행렬 버퍼 업로드(본 수 x 64B). 정점 변환은 Base Pass의 VS가 한다.
// 그래서 CPU 쪽 비용은 CPU 시간, GPU 쪽 비용은 Base Pass GPU 시간에 나타난다.
struct FSkinningFrameStats
{
	uint32 CpuSkinnedComponents = 0;   // 이번 프레임에 CPU로 정점을 다시 만든 컴포넌트 수
	uint32 GpuSkinnedComponents = 0;   // 이번 프레임에 본 행렬만 올린 컴포넌트 수
	uint64 CpuSkinnedVertices = 0;     // CPU가 변환한 정점 수 (LOD 포함)
	uint64 BoneMatrices = 0;           // 다시 계산한 본 행렬 수

	uint64 VertexUploadBytes = 0;      // CPU 스키닝 결과 정점 버퍼 업로드량
	uint64 MatrixUploadBytes = 0;      // GPU 스키닝 본 행렬 버퍼 업로드량

	uint64 BoneMatrixCycles = 0;       // 본 계층 → 스키닝 행렬 (두 모드 공통)
	uint64 CpuSkinCycles = 0;          // 정점 변환 (CPU 모드만)
	uint64 UploadCycles = 0;           // Map/Unmap 업로드

	// 프레임 끝에 장면의 스켈레탈 메시 컴포넌트를 세어 채운다.
	uint32 TotalComponents = 0;
	uint32 CpuModeComponents = 0;
	uint32 GpuModeComponents = 0;
	uint64 TotalVertices = 0;
	uint64 TotalBones = 0;
};

class FSkinningStats
{
public:
	static bool IsCollecting() { return FStatOverlay::IsEnabled(EStatFlags::Skinning); }

	static FSkinningFrameStats& Pending() { return PendingFrame; }
	static const FSkinningFrameStats& Last() { return LastFrame; }

	static const FCycleStatData& BoneMatrixHistory() { return BoneMatrixData; }
	static const FCycleStatData& CpuSkinHistory() { return CpuSkinData; }
	static const FCycleStatData& UploadHistory() { return UploadData; }
	static const FCycleStatData& TotalHistory() { return TotalData; }

	// 프레임 끝: 이번 프레임 값을 표시용으로 옮기고 120프레임 이력에 넣는다.
	static void EndFrame(const FSkinningFrameStats& SceneCounts)
	{
		if (!IsCollecting())
		{
			PendingFrame = {};
			return;
		}
		PendingFrame.TotalComponents = SceneCounts.TotalComponents;
		PendingFrame.CpuModeComponents = SceneCounts.CpuModeComponents;
		PendingFrame.GpuModeComponents = SceneCounts.GpuModeComponents;
		PendingFrame.TotalVertices = SceneCounts.TotalVertices;
		PendingFrame.TotalBones = SceneCounts.TotalBones;

		BoneMatrixData.PushFrame(PendingFrame.BoneMatrixCycles);
		CpuSkinData.PushFrame(PendingFrame.CpuSkinCycles);
		UploadData.PushFrame(PendingFrame.UploadCycles);
		TotalData.PushFrame(PendingFrame.BoneMatrixCycles + PendingFrame.CpuSkinCycles + PendingFrame.UploadCycles);

		LastFrame = PendingFrame;
		PendingFrame = {};
	}

	static void ResetHistory()
	{
		BoneMatrixData = {};
		CpuSkinData = {};
		UploadData = {};
		TotalData = {};
	}

	// 모드를 바꾸면 앞 모드의 표본이 평균에 섞이지 않게 이력을 비운다.
	static void NotifyModeChanged(ESkinningMode Mode)
	{
		if (Mode == LastMode) return;
		LastMode = Mode;
		ResetHistory();
	}

private:
	inline static FSkinningFrameStats PendingFrame{};
	inline static FSkinningFrameStats LastFrame{};
	inline static FCycleStatData BoneMatrixData{};
	inline static FCycleStatData CpuSkinData{};
	inline static FCycleStatData UploadData{};
	inline static FCycleStatData TotalData{};
	inline static ESkinningMode LastMode = ESkinningMode::CPU;
};

// stat skinning이 켜졌을 때만 사이클을 재서 Destination에 더한다 (전역 Profile 설정과 무관).
class FSkinningCpuScope
{
public:
	explicit FSkinningCpuScope(uint64& InDestination)
		: Destination(InDestination)
	{
		if (FSkinningStats::IsCollecting())
			Timer.emplace(TStatId{}, true);
	}

	~FSkinningCpuScope()
	{
		if (Timer)
			Destination += Timer->Finish();
	}

	FSkinningCpuScope(const FSkinningCpuScope&) = delete;
	FSkinningCpuScope& operator=(const FSkinningCpuScope&) = delete;

private:
	uint64& Destination;
	std::optional<FScopeCycleCounter> Timer;
};
