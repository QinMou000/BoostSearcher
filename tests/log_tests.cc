#include "log.h"

#include <iostream>
#include <regex>
#include <sstream>

int main() {
    std::ostringstream captured;
    std::ostream *original_output = GetLogOutputStream();
    GetLogOutputStream() = &captured;
    {
        LOG(LogLevel::INFO) << "标准时间格式测试";
    }
    GetLogOutputStream() = original_output;

    // 校验完整日志行，锁住日期、时间、分隔符和原有字段顺序。
    const std::regex pattern(
        R"(\[INFO\] \[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] \[标准时间格式测试\] \[.+ : \d+\]\r?\n)");
    if (!std::regex_match(captured.str(), pattern)) {
        std::cerr << "日志时间格式不符合 YYYY-MM-DD HH:MM:SS：" << captured.str();
        return 1;
    }

    std::cout << "日志时间格式测试通过" << std::endl;
    return 0;
}
