# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

Boost Searcher — 三层架构的 Boost 文档全文搜索引擎，支持模糊搜索和 AI 增强搜索。

- **engine/**：C++17 搜索引擎（索引构建 + 搜索 + HTTP 接口）
- **server/**：Python FastAPI 中间层（API 路由 + SSE + AI 集成）（待建）
- **web/**：React 前端（待建）

## 构建与运行

```bash
# 编译 C++ 搜索引擎
cd engine && mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 离线步骤：解析文档生成原始语料
./parser                         # 输入 data/input/*.html → 输出 data/raw_html/raw.txt

# 启动 HTTP 搜索服务
./http_server                    # 监听 0.0.0.0:8080

# 命令行调试模式
./debug                          # 终端交互输入关键词，输出 JSON
```

C++ 编译依赖：g++ (C++17)、CMake 3.14+。所有第三方库已内嵌（cppjieba、nlohmann/json、cpp-httplib），无需系统安装。

## 数据流与核心架构

```
文档 (data/input/)  →  parser.cc  →  raw.txt (ETX 分隔: title\3content\3url\n)
                                              │
                                              ▼
                                    index.hpp (单例 Index)
                                    ├── 正排索引: vector<DocInfo>，下标即 doc_id，O(1) 查找
                                    └── 倒排索引: unordered_map<word, InvertedList>，O(1) 查找
                                              │
                                              ▼
                                    searcher.hpp (Searcher)
                                    ├── jieba 分词 (CutForSearch 模式)
                                    ├── 倒排合并，按 doc_id 汇总权重
                                    ├── 排序 + 截取 top-100
                                    └── nlohmann/json 序列化
                                              │
                                              ▼
                                    http_server.cc → /s?word=xxx 返回 JSON
                                              │
                                              ▼
                                    Python FastAPI → AI 增强 → SSE 流式推送到前端
```

权重公式：title 词频 × 10 + content 词频 × 1。

## 代码约定

- C++17 标准，头文件守卫用 `#pragma once`
- `.hpp` 包含类定义与实现（header-only），`.cc` 仅含 main 入口
- 命名空间：`ns_index::`（索引）、`ns_searcher::`（搜索）
- 工具类全静态方法：`File_Util`、`String_Util`、`Jieba_util`
- 日志：`LOG(LogLevel::LEVEL) << message`（RAII，析构时输出，线程安全）
- 单例：`Index::GetInstance()`，双重检查锁
- raw.txt 行格式：`title\3content\3url\n`（ETX 分隔符）
- 第三方库头文件放在 `engine/third/`，用系统 include 路径引用

## 关键文件速查

| 文件 | 职责 |
|------|------|
| `engine/src/parser.cc` | 离线解析 Boost HTML，输出 raw.txt |
| `engine/src/index.hpp` | 正排/倒排索引，单例 |
| `engine/src/searcher.hpp` | 分词、倒排合并、排序、摘要、JSON 序列化 |
| `engine/src/http_server.cc` | HTTP 服务，/s 搜索接口 |
| `engine/src/debug.cc` | CLI 调试入口 |
| `engine/src/util.hpp` | 文件读写、字符串拆分、cppjieba 封装 |
| `engine/src/log.hpp` | RAII 日志，支持文件输出 |
| `engine/src/daemon.hpp` | 进程守护化 |
| `engine/third/httplib.h` | 第三方单文件 HTTP 库（勿修改） |
| `engine/third/nlohmann/json.hpp` | 第三方 JSON 库（勿修改） |

## Trellis 工作流

本项目使用 Trellis 管理开发流程。关键路径：

- `.trellis/workflow.md` — 开发阶段定义、任务创建与技能路由
- `.trellis/spec/` — 分层编码规范（backend/frontend/guides）
- `.trellis/scripts/task.py` — 任务生命周期管理
- `.claude/settings.json` — Hook 配置（SessionStart、PreToolUse、UserPromptSubmit）

创建任务：`python3 .trellis/scripts/task.py create "<title>"`，然后通过 trellis-brainstorm 技能迭代 PRD。
