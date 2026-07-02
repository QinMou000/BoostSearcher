#pragma once
#include <signal.h>
#include <stdlib.h>
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif
#include "log.hpp"

#ifdef _WIN32
void daemon() {
    // Windows 下开发调试时保持前台运行
}
#else
// 将当前进程转为守护进程
void daemon() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    if (fork() > 0) {
        exit(0);
    }

    setsid();

    // 将标准输入/输出/错误重定向到 /dev/null
    int fd = ::open("/dev/null", O_RDWR);
    if (fd < 0) {
        LOG(LogLevel::WARNING) << "open /dev/null error";
    } else {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }
}
#endif
