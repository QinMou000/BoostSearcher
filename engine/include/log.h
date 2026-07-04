#pragma once

#include <ctime>
#include <iosfwd>
#include <mutex>
#include <sstream>
#include <string>

enum class LogLevel { NORMAL, INFO, DEBUG, WARNING, LOG_ERROR, FATAL };

const char *LogLevelToString(LogLevel level);

std::ostream *&GetLogOutputStream();

std::mutex &GetLogMutex();

// RAII 日志消息对象：构造时记录日志元信息，析构时统一输出完整日志行。
class LogMessage {
  public:
    LogMessage(LogLevel level, const char *file, int line);

    // 模板函数必须保留在头文件中，调用点才能根据实际参数类型完成实例化。
    template <typename T> LogMessage &operator<<(const T &value) {
        ss_ << value;
        return *this;
    }

    ~LogMessage();

  private:
    LogLevel level_;
    const char *file_;
    int line_;
    std::ostringstream ss_;
};

#define LOG(level) LogMessage(level, __FILE__, __LINE__)

// 将后续日志输出重定向到文件；文件流生命周期由实现文件中的静态对象托管。
void FileLogStrategy(const std::string &filename = "http.log");
