#include "logging/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace spacestation
{
namespace
{
const char* LevelLabel(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}
} // namespace

Logger& Logger::Instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    Shutdown();
}

void Logger::Init(LogLevel level, const std::filesystem::path& file_path)
{
    std::lock_guard lock(mutex_);
    level_ = level;
    if (file_.is_open())
    {
        file_.close();
    }
    std::filesystem::create_directories(file_path.parent_path());
    file_.open(file_path, std::ios::app);
}

void Logger::Shutdown()
{
    std::lock_guard lock(mutex_);
    if (file_.is_open())
    {
        file_.flush();
        file_.close();
    }
}

void Logger::SetLevel(LogLevel level)
{
    std::lock_guard lock(mutex_);
    level_ = level;
}

void Logger::Write(LogLevel level, const char* file, int line, const char* function, const char* message)
{
    std::lock_guard lock(mutex_);
    if (level < level_)
    {
        return;
    }

    const auto rendered = Render(level, file, line, function, message);
    std::cout << rendered << std::endl;
    if (file_.is_open())
    {
        file_ << rendered << '\n';
        file_.flush();
    }
}

std::string Logger::Render(LogLevel level, const char* file, int line, const char* function, const char* message) const
{
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    local = *std::localtime(&time);
#endif

    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.';
    stream << std::setw(3) << std::setfill('0') << millis.count();
    stream << " [" << LevelLabel(level) << "] " << file << ':' << line << ' ' << function << ' ' << message;
    return stream.str();
}

LogLevel ParseLogLevel(const std::string& level)
{
    if (level == "trace") return LogLevel::Trace;
    if (level == "debug") return LogLevel::Debug;
    if (level == "warn") return LogLevel::Warn;
    if (level == "error") return LogLevel::Error;
    return LogLevel::Info;
}

std::string ToLogLevelString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "info";
}
} // namespace spacestation
