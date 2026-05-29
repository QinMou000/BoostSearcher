# Boost Searcher 重构：模糊搜索 + AI 增强 + 高并发 + MD 收录

## Goal

将现有的 Boost HTML 文档搜索引擎重构为通用的 Markdown 文档搜索平台，支持模糊搜索、AI 增强搜索（类似 Bing 的 AI 摘要）、高并发后端、易改前端。

## Architecture Decision

**三层架构：C++ 搜索引擎 + Python FastAPI 中间层 + React 前端**

```
浏览器 (React)
   │  GET /search?q=xxx (SSE)
   ▼
Python FastAPI (中间层)
   ├─→ C++ 搜索引擎 (HTTP, localhost:8080) → 返回 JSON
   ├─→ 立即通过 SSE 推送搜索结果给前端
   └─→ 异步调 AI API → AI 摘要完成后通过 SSE 追加推送
```

- C++ 负责：索引构建、搜索、模糊匹配（核心算法）
- Python 负责：API 路由、SSE 流式推送、AI API 调用
- React 负责：用户界面、SSE 消费、结果渲染

## Requirements

### 1. C++ 搜索引擎 (engine/)
* C++17 标准，零外部系统依赖（仅 cppjieba 在仓库内）
* jsoncpp → nlohmann/json（单头文件）
* pthread → std::thread/std::mutex（C++11 标准库）
* Boost (filesystem/string) → std::filesystem + std::transform
* Makefile → CMakeLists.txt
* parser 支持 MD 文档解析（标题/内容/URL 提取）
* 索引层加 Bigram 模糊搜索
* HTTP JSON 接口（/search?q=xxx）

### 2. Python FastAPI 中间层 (server/)
* 路由转发 + SSE 流式响应
* AI API 调用（Claude/OpenAI）
* 搜索结果先返回，AI 摘要异步追加

### 3. React 前端 (web/)
* TypeScript + Vite
* 组件化：SearchBar / SearchResults / AiSummary
* SSE 消费 hook (useSearch)

## Decision (ADR-lite)

**Context**: 项目是简历项目，核心必须保持 C++，同时支持 AI 增强搜索
**Decision**: C++ 搜索引擎 + Python FastAPI 中间层 + React 前端；C++ 依赖全部替换为标准库/头文件
**Consequences**: C++ 代码量不变，但依赖管理大幅简化；Python 层引入额外进程，但职责清晰

## Out of Scope (explicit)

* 分布式/集群部署
* 用户认证/权限系统
* Docker 部署（后续添加）
* 多租户支持

## Phase 1: C++ 引擎重构（当前任务）

将现有 C++ 代码迁移到 engine/ 目录，替换依赖，确保编译通过运行正常。

### 子任务

1. **目录迁移**: 源码 → engine/src/，词典 → engine/dict/，头文件 → engine/include/
2. **依赖替换**:
   - jsoncpp → nlohmann/json (third/json.hpp)
   - Boost::filesystem → std::filesystem (C++17)
   - boost::to_lower → std::transform + ::tolower
   - pthread → std::thread/std::mutex (已有，移除 pthread 链接)
3. **CMakeLists.txt**: 替代 Makefile，支持 out-of-source 构建
4. **编译验证**: cmake .. && make，运行 parser + http_server + debug
