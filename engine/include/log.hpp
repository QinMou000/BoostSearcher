#pragma once
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

enum class LogLevel { NORMAL, INFO, DEBUG, WARNING, LOG_ERROR, FATAL };

// clang-format off
inline const char *LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::NORMAL:  return "NORMAL";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::LOG_ERROR: return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "UNKNOWN";
    }
}
// clang-format on

inline std::ostream *&GetLogOutputStream() {
    static std::ostream *stream = &std::cout;
    return stream;
}

inline std::mutex &GetLogMutex() {
    static std::mutex mtx;
    return mtx;
}

// RAII 日志消息：构造时记录元信息，析构时输出完整日志行
class LogMessage {
  public:
    LogMessage(LogLevel level, const char *file, int line)
        : level_(level), file_(file), line_(line) {}

    template <typename T> LogMessage &operator<<(const T &value) {
        ss_ << value;
        return *this;
    }

    ~LogMessage() {
        std::lock_guard<std::mutex> lock(GetLogMutex());
        *GetLogOutputStream()
            << "[" << LogLevelToString(level_) << "] "
            << "[" << std::time(nullptr) << "] "
            << "[" << ss_.str() << "] "
            << "[" << file_ << " : " << line_ << "]" << std::endl;
    }

  private:
    LogLevel level_;
    const char *file_;
    int line_;
    std::ostringstream ss_;
};

#define LOG(level) LogMessage(level, __FILE__, __LINE__)

// 将日志输出重定向到文件
inline void FileLogStrategy(const std::string &filename = "http.log") {
    static std::ofstream file_stream;
    file_stream.open(filename, std::ios::app);
    if (file_stream.is_open()) {
        GetLogOutputStream() = &file_stream;
    }
}
