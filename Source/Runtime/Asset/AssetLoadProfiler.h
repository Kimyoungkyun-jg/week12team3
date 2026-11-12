#pragma once

#include "Core/Windows/WindowsPlatformTime.h"

#include <algorithm>
#include <fstream>

// 시작 로딩의 구간별 시간을 모은다. 환경 변수 HITORI_LOAD_PROFILE에 경로가 있을 때만 보고서를 쓴다.
// 로딩은 TaskPool을 만들기 전 메인 스레드에서만 돌기 때문에 동기화하지 않는다.
class FAssetLoadProfiler
{
public:
	static void AddStage(const FString& Name, double Ms)
	{
		AddTo(Stages, Name, Ms);
	}

	static void AddFile(const FString& Path, double Ms)
	{
		AddTo(Files, Path, Ms);
	}

	static void WriteReport()
	{
		char Path[512]{};
		if (GetEnvironmentVariableA("HITORI_LOAD_PROFILE", Path, sizeof(Path)) == 0)
		{
			return;
		}

		std::ofstream Out(Path);
		Out << "[Stages]\n";
		for (const FEntry& Entry : Stages)
		{
			Out << Entry.Name << '\t' << Entry.Ms << '\t' << Entry.Count << '\n';
		}

		std::sort(Files.GetData(), Files.GetData() + Files.Num(), [](const FEntry& A, const FEntry& B) { return A.Ms > B.Ms; });
		Out << "[Files]\n";
		for (const FEntry& Entry : Files)
		{
			Out << Entry.Name << '\t' << Entry.Ms << '\t' << Entry.Count << '\n';
		}
	}

private:
	struct FEntry
	{
		FString Name;
		double Ms = 0.0;
		int32 Count = 0;
	};

	static void AddTo(TArray<FEntry>& Entries, const FString& Name, double Ms)
	{
		for (FEntry& Entry : Entries)
		{
			if (Entry.Name == Name)
			{
				Entry.Ms += Ms;
				++Entry.Count;
				return;
			}
		}
		Entries.Add(FEntry{ Name, Ms, 1 });
	}

	static inline TArray<FEntry> Stages;
	static inline TArray<FEntry> Files;
};

// 소멸하거나 Stop을 부를 때까지의 시간을 구간에 더한다.
class FLoadStageTimer
{
public:
	explicit FLoadStageTimer(const char* InName) : Name(InName), StartCycles(FPlatformTime::GetCycles64()) {}
	~FLoadStageTimer() { Stop(); }

	void Stop()
	{
		if (Name)
		{
			FAssetLoadProfiler::AddStage(Name, FPlatformTime::ToMilliseconds64(FPlatformTime::GetCycles64() - StartCycles));
			Name = nullptr;
		}
	}

private:
	const char* Name;
	uint64 StartCycles;
};
