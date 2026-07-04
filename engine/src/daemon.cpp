#include "daemon.h"

#include "log.h"

#include <csignal>
#include <cstdlib>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
void daemon() {
    // Windows 下开发调试时保持前台运行。
}
#else
void daemon() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    if (fork() > 0) {
        exit(0);
    }

    setsid();

    // 守护进程不应继续占用终端标准输入输出，统一重定向到 /dev/null。
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
