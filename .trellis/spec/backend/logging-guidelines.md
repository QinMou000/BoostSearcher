# Logging Guidelines

> How logging is done in this project.

---

## Overview

<!--
Document your project's logging conventions here.

Questions to answer:
- What logging library do you use?
- What are the log levels and when to use each?
- What should be logged?
- What should NOT be logged (PII, secrets)?
-->

(To be filled by the team)

---

## Log Levels

<!-- When to use each level: debug, info, warn, error -->

(To be filled by the team)

---

## Structured Logging

<!-- Log format, required fields -->

(To be filled by the team)

---

## What to Log

<!-- Important events to log -->

(To be filled by the team)

---

## What NOT to Log

<!-- Sensitive data, PII, secrets -->

(To be filled by the team)

---

## 场景：统一日志时间格式

### 1. 范围与触发条件

* 触发条件：修改 `engine/src/log.cpp` 的统一日志输出格式。
* 适用范围：所有经 `LOG(LogLevel::...)` 宏输出的日志行。

### 2. 接口

* 日志调用接口保持 `LOG(level) << message` 不变。
* 日志行字段顺序保持为：级别、时间、消息、文件与行号。

### 3. 输出契约

* 时间使用本地时间，固定格式为 `YYYY-MM-DD HH:MM:SS`。
* Windows 使用 `localtime_s`，其他平台使用 `localtime_r`，避免复用非线程安全的静态时间缓冲区。
* 日志输出继续在 `GetLogMutex()` 保护范围内完成。

### 4. 验证与异常

* 时间转换成功 -> 输出形如 `[2026-08-16 20:15:30]` 的时间字段。
* 时间转换失败 -> 输出固定宽度的 `[0000-00-00 00:00:00]`，避免破坏日志解析。

### 5. 正反示例

正确：`[INFO] [2026-08-16 20:15:30] [建立索引成功] [file : 42]`。

错误：`[INFO] [1786892130] [建立索引成功] [file : 42]`，纯 Unix 时间戳不便于人工排查。

### 6. 测试要求

* `log_tests` 必须验证完整日志行中的时间字段匹配 `YYYY-MM-DD HH:MM:SS`。
* 修改日志模块后仍须运行既有 `searcher_tests`。

### 7. 错误与正确实现

```cpp
// 错误：直接输出 Unix 时间戳。
stream << std::time(nullptr);

// 正确：线程安全转换后格式化为固定宽度本地时间。
stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
```
