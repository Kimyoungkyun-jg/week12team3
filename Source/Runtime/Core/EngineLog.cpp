#include "EnginePCH.h"
#include "EngineLog.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace
{
    struct FPendingLog
    {
        ELogVerbosity Verbosity;
        FString Message;
    };

    // 싱크는 초기화 중에 하나씩 붙는다. 늦게 붙은 싱크도 시작부터의 로그를 받도록 앞부분을 보관한다.
    // 싱크가 끝내 붙지 않는 Game 빌드에서 메모리가 늘지 않게 개수를 제한한다.
    constexpr int32 MaxPendingLogs = 4096;

    struct FLogState
    {
        // 작업 스레드에서도 로그를 찍는다. 싱크가 OnLog 안에서 다시 로그를 찍을 수 있어 재진입 가능한 mutex를 쓴다.
        std::recursive_mutex Mutex;
        TArray<ILogSink*> Sinks;
        TArray<FPendingLog> Pending;
        int32 DroppedCount = 0;
    };

    // 정적 소멸 순서와 관계없이 종료 중 로그도 안전하도록 해제하지 않는다.
    FLogState& GetLogState()
    {
        static FLogState* State = new FLogState();
        return *State;
    }

    // 직전 실행의 로그를 "<이름>-backup-<마지막 수정 시각>.log"로 바꾸고, 오래된 backup을 지운다.
    // 시각은 "YYYY.MM.DD-HH.MM.SS" 형식이라 이름순 정렬이 곧 시간순 정렬이다.
    void RotateLogFile(const fs::path& Path, int32 MaxBackups)
    {
        std::error_code Error;
        const fs::path Folder = Path.parent_path();
        const std::wstring Prefix = Path.stem().wstring() + L"-backup-";
        if (fs::exists(Path, Error))
        {
            const auto WriteTime = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(Path, Error));
            const auto Local = std::chrono::current_zone()->to_local(std::chrono::floor<std::chrono::seconds>(WriteTime));
            const FString Stamp = std::format("{:%Y.%m.%d-%H.%M.%S}", Local);
            const std::wstring Base = Prefix + fs::path(Stamp).wstring();
            // 같은 초에 두 번 실행해도 이름이 겹치지 않게 번호를 붙인다.
            fs::path Backup = Folder / (Base + L".log");
            for (int32 Suffix = 1; fs::exists(Backup, Error); ++Suffix)
                Backup = Folder / (Base + L"_" + std::to_wstring(Suffix) + L".log");
            fs::rename(Path, Backup, Error);
        }

        std::vector<fs::path> Backups;
        for (const fs::directory_entry& Entry : fs::directory_iterator(Folder, Error))
            if (Entry.is_regular_file(Error) && Entry.path().extension() == L".log" && Entry.path().filename().wstring().starts_with(Prefix))
                Backups.push_back(Entry.path());
        if (Backups.size() <= static_cast<size_t>(MaxBackups))
            return;
        std::sort(Backups.begin(), Backups.end());
        for (size_t Index = 0; Index < Backups.size() - MaxBackups; ++Index)
            fs::remove(Backups[Index], Error);
    }
}

void FLog::AddSink(ILogSink* Sink)
{
    if (!Sink)
        return;

    FLogState& State = GetLogState();
    std::lock_guard Lock(State.Mutex);
    // 등록 전에 넘겨야 다시 찍힌 로그가 같은 싱크에 먼저 가지 않는다. OnLog 안의 로그로 배열이 커져도 되도록 인덱스로 돈다.
    const int32 Count = State.Pending.Num();
    for (int32 Index = 0; Index < Count; ++Index)
        Sink->OnLog(State.Pending[Index].Verbosity, State.Pending[Index].Message);
    if (State.DroppedCount > 0)
        Sink->OnLog(ELogVerbosity::Warning, std::format("[Log] {} early log lines were dropped", State.DroppedCount));
    State.Sinks.Add(Sink);
}

void FLog::RemoveSink(ILogSink* Sink)
{
    FLogState& State = GetLogState();
    std::lock_guard Lock(State.Mutex);
    for (int32 Index = State.Sinks.Num() - 1; Index >= 0; --Index)
        if (State.Sinks[Index] == Sink)
            State.Sinks.RemoveAt(Index, 1);
}

FFileLogSink::FFileLogSink(const FString& InPath, int32 MaxBackups)
{
    const std::filesystem::path Path(InPath);
    std::error_code Error;
    if (Path.has_parent_path())
        std::filesystem::create_directories(Path.parent_path(), Error);
    if (MaxBackups > 0 && Path.has_parent_path())
        RotateLogFile(Path, MaxBackups);
    File.open(Path, std::ios::out | std::ios::trunc);
}

void FFileLogSink::OnLog(ELogVerbosity Verbosity, const FString& Message)
{
    if (!File.is_open())
        return;

    const char* Label = "Info";
    switch (Verbosity)
    {
    case ELogVerbosity::Input:   Label = "Input"; break;
    case ELogVerbosity::Warning: Label = "Warning"; break;
    case ELogVerbosity::Error:   Label = "Error"; break;
    default: break;
    }

    std::lock_guard Lock(Mutex);
    File << '[' << Label << "] " << Message << '\n';
    File.flush();
}

void FLog::Emit(ELogVerbosity Verbosity, const FString& Message)
{
    FLogState& State = GetLogState();
    std::lock_guard Lock(State.Mutex);
    if (State.Pending.Num() < MaxPendingLogs)
        State.Pending.Add({ Verbosity, Message });
    else
        ++State.DroppedCount;
    // OnLog 안에서 싱크가 늘거나 줄어도 되도록 인덱스로 돈다.
    for (int32 Index = 0; Index < State.Sinks.Num(); ++Index)
        State.Sinks[Index]->OnLog(Verbosity, Message);
}