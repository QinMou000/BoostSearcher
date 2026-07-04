#pragma once

// 将当前进程转为守护进程；Windows 开发环境下保持前台运行，便于调试。
void daemon();
