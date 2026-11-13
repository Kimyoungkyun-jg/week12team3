#pragma once

#include <fstream>
#include <mutex>

enum class ELogVerbosity
{
	Input,
	Info,
	Warning,
	Error
};

class ILogSink
{
public:
	virtual void OnLog(ELogVerbosity, const FString&) = 0;
};

// 로그를 파일에 남기고 줄마다 flush한다. 최신 로그는 항상 InPath이고, MaxBackups가 있으면 직전 로그를 그 개수까지 보관한다.
class FFileLogSink : public ILogSink
{
public:
	explicit FFileLogSink(const FString& InPath, int32 MaxBackups = 0);
	virtual void OnLog(ELogVerbosity Verbosity, const FString& Message) override;
	bool IsOpen() const { return File.is_open(); }

private:
	std::mutex Mutex;
	std::ofstream File;
};

class FLog
{
public:
    static void AddSink(ILogSink* Sink);
    static void RemoveSink(ILogSink* Sink);
    static void Emit(ELogVerbosity Verbosity, const FString& Message);

    template <typename... Args>
    static void Log(ELogVerbosity Verbosity, std::format_string<Args...> Fmt, Args&&... InArgs)
    {
        Emit(Verbosity, std::format(Fmt, std::forward<Args>(InArgs)...));
    }
};

#define HTR_LOG(Verbosity, ...) FLog::Log(ELogVerbosity::Verbosity, __VA_ARGS__)
