#include "log.h"

#include <fstream>
#include <iostream>

// 日志级别字符串集中放在实现文件，避免每个包含头文件的翻译单元重复生成 switch 代码。
const char *LogLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::NORMAL: return "NORMAL";
    case LogLevel::INFO: return "INFO";
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::WARNING: return "WARNING";
    case LogLevel::LOG_ERROR: return "ERROR";
    case LogLevel::FATAL: return "FATAL";
    default: return "UNKNOWN";
    }
}

std::ostream *&GetLogOutputStream() {
    static std::ostream *stream = &std::cout;
    return stream;
}

std::mutex &GetLogMutex() {
    static std::mutex mtx;
    return mtx;
}

LogMessage::LogMessage(LogLevel level, const char *file, int line) : level_(level), file_(file), line_(line) {}

LogMessage::~LogMessage() {
    std::lock_guard<std::mutex> lock(GetLogMutex());
    *GetLogOutputStream() << "[" << LogLevelToString(level_) << "] "
                          << "[" << std::time(nullptr) << "] "
                          << "[" << ss_.str() << "] "
                          << "[" << file_ << " : " << line_ << "]" << std::endl;
}

void FileLogStrategy(const std::string &filename) {
    static std::ofstream file_stream;
    file_stream.open(filename, std::ios::app);
    if (file_stream.is_open()) {
        GetLogOutputStream() = &file_stream;
    }
}
