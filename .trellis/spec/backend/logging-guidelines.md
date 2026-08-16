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

---

## 场景：独立 Python 工具日志

### 1. 范围与触发条件

* 触发条件：新增或修改 `tools/` 下可独立运行的 Python 工具，且工具需要保留运行过程或错误信息。
* 适用范围：工具入口、其对应的独立 `.log` 文件及离线单元测试。
* 不适用范围：HTTP 服务的 `http.log`；工具不得向该文件或其他工具的日志文件写入内容。

### 2. 签名

* 日志路径函数：`log_file_path(script_path: Path | None = None) -> Path`。
* 日志器初始化函数：`configure_file_logger(log_path: Path | None = None) -> logging.Logger`。
* 输出桥接函数：`print_and_log(message: str, logger: logging.Logger) -> None`。

### 3. 契约

* 日志文件名必须由脚本文件名推导，形式为 `<仓库根目录>/<脚本名>.log`；例如 `tools/sync_gitee_posts.py` 对应 `sync_gitee_posts.log`。
* 使用标准库 `logging.FileHandler` 以 UTF-8 追加写入，关闭向根日志器传播；不得引入第三方日志依赖。
* 日志行固定为 `[级别] [YYYY-MM-DD HH:MM:SS] [消息] [文件 : 行号]`，与 C++ 运行时日志字段顺序一致。
* 命令行原有标准输出与标准错误保持不变；入口层使用桥接函数将同一同步事件写入终端和独立日志。
* 命令结束时必须关闭并移除文件处理器，避免 Windows 在同一进程中锁住日志文件。

### 4. 验证与错误矩阵

* 同一目标路径重复初始化 -> 复用已有处理器，不重复写入同一事件。
* 日志文件无法创建或打开 -> 标准错误输出初始化失败原因，返回非零，且不执行同步。
* 工具捕获 `SyncError` -> 保持原标准错误提示，并追加 `ERROR` 级别日志。
* 单篇同步输出 `[失败]` -> 日志使用 `ERROR`；其他既有进度输出使用 `INFO`。
* 命令结束 -> 释放 `FileHandler`，临时目录及后续调度任务可删除或重开日志文件。

### 5. 正常、基础与错误案例

* 正常：`python -B tools/sync_gitee_posts.py --dry-run` 写入 `sync_gitee_posts.log`，包含启动和预览汇总信息。
* 基础：离线测试注入临时日志路径，断言完整时间格式与单个处理器。
* 错误：把工具日志写入 `http.log`，导致 HTTP 服务与同步工具的事件无法区分。
* 错误：只调用 `logging.basicConfig`，使多个工具共享根日志器或在重复调用时产生重复记录。

### 6. 必需测试

* 断言日志路径由工具脚本名推导，且不等于 `http.log`。
* 断言格式匹配时间、级别、消息、Python 文件名和行号。
* 断言正常入口事件和 `SyncError` 均写入临时日志文件，且终端输出与退出码保持契约。
* 断言同一路径重复初始化只有一个处理器；测试结束前关闭处理器，确保 Windows 可清理临时目录。

### 7. 错误与正确实现

#### 错误

```python
logging.basicConfig(filename=repository_root() / "http.log")
```

#### 正确

```python
log_path = repository_root() / f"{Path(__file__).stem}.log"
file_handler = logging.FileHandler(log_path, encoding="utf-8")
file_handler.setFormatter(logging.Formatter(LOG_FORMAT, datefmt=LOG_DATE_FORMAT))
```
