#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace spacestation
{
enum class LogLevel
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

class Logger
{
  public:
    static Logger& Instance();

    void Init(LogLevel level, const std::filesystem::path& file_path);
    void Shutdown();
    void SetLevel(LogLevel level);
    void Write(LogLevel level, const char* file, int line, const char* function, const char* message);

  private:
    Logger() = default;
    ~Logger();

    std::string Render(LogLevel level, const char* file, int line, const char* function, const char* message) const;

    std::mutex mutex_;
    std::ofstream file_;
    LogLevel level_ = LogLevel::Info;
};

LogLevel ParseLogLevel(const std::string& level);
std::string ToLogLevelString(LogLevel level);
} // namespace spacestation

#define logT(message) spacestation::Logger::Instance().Write(spacestation::LogLevel::Trace, __FILE__, __LINE__, __func__, message)
#define logD(message) spacestation::Logger::Instance().Write(spacestation::LogLevel::Debug, __FILE__, __LINE__, __func__, message)
#define logI(message) spacestation::Logger::Instance().Write(spacestation::LogLevel::Info, __FILE__, __LINE__, __func__, message)
#define logW(message) spacestation::Logger::Instance().Write(spacestation::LogLevel::Warn, __FILE__, __LINE__, __func__, message)
#define logE(message) spacestation::Logger::Instance().Write(spacestation::LogLevel::Error, __FILE__, __LINE__, __func__, message)
