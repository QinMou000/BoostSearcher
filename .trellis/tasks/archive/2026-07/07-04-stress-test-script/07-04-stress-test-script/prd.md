# 为项目添加压力测试脚本

## Goal

在 `tests/` 目录下新增一个面向本项目的压力测试脚本，用于自动生成可控规模的搜索语料，复用现有 CMake 构建产物，对命令行搜索入口和 HTTP 搜索入口进行可重复、本地化的压力验证，并输出吞吐、延迟、错误数等指标。

## What I Already Know

* 用户要求在测试目录下写一个“全面一点”的压力测试脚本。
* 仓库现有测试目录为 `tests/`，现有测试入口是 `tests/searcher_tests.cc`。
* 项目构建入口是 `engine/CMakeLists.txt`，使用 CMake 生成 `parser`、`debug`、`http_server`、`searcher_tests`。
* 搜索核心接口是 `ns_searcher::Searcher::InitSearcher(raw_path)` 和 `Searcher::Search(query, &json)`。
* `debug` 固定读取运行目录下的 `./data/raw.txt`，通过标准输入循环接收查询。
* `http_server` 固定读取运行目录下的 `./data/raw.txt`，并通过 `/s?word=<关键词>` 返回 JSON 搜索结果。
* 现有 C++ 测试已经有小规模并发搜索与指标输出，新增脚本应覆盖更大规模、更接近端到端的压力路径。

## Requirements

* 在 `tests/` 下新增压力测试脚本，不引入第三方 Python 依赖。
* 脚本必须可配置语料数量、查询轮数、并发数、超时时间、构建目录和是否跳过构建。
* 脚本必须在 `build/stress-work/` 运行目录中生成 `data/raw.txt`，不得覆盖仓库真实 `data/` 内容。
* 脚本必须复用项目现有 CMake 构建方式，能够按需构建 `debug` 和 `http_server`。
* 脚本必须覆盖多类查询：精确命中、短语命中、模糊召回、英文大小写、停用词/空结果、单字查询、长查询和不存在词。
* 脚本必须输出命令行搜索压测指标：总查询数、耗时、QPS、平均延迟、失败数。
* 脚本必须支持 HTTP 压测：优先使用传入的 `--http-url`；在 Windows 上可自动启动本地 `http_server`；在非 Windows 且未提供 URL 时安全跳过自动启动。
* 脚本必须对关键查询做结果校验，失败时返回非零退出码。
* 所有提示、错误、注释和说明性文本使用简体中文。

## Acceptance Criteria

* [ ] 执行 `python tests/stress_searcher.py --docs 200 --queries 120 --threads 4 --skip-http` 可以完成命令行压测并返回 0。
* [ ] 执行脚本时不会修改仓库真实 `data/raw.txt`；兼容旧二进制时只允许写入被忽略的 `build/data/raw.txt`。
* [ ] 构建失败、二进制缺失、查询校验失败或 HTTP 错误会返回非零退出码。
* [ ] 指标输出至少包含总查询数、耗时、QPS、平均延迟、失败数。
* [ ] 脚本只依赖 Python 标准库和项目现有 CMake 构建系统。

## Definition of Done

* 新增脚本已放在 `tests/` 目录。
* 本地执行最小规模压力测试通过。
* 本地执行现有 `searcher_tests` 通过，确保未破坏既有功能。
* 若 HTTP 自动启动在当前平台不可用，需在最终说明中记录原因和可替代验证方式。

## Technical Approach

采用 Python 标准库实现编排脚本。脚本负责生成临时语料、定位或构建二进制、运行 `debug` 进行批量查询压测，并通过 `urllib` 与 `ThreadPoolExecutor` 对 HTTP 接口做并发请求。这样无需改动核心 C++ 代码，也不会引入额外依赖。

## Decision (ADR-lite)

**Context**: 项目已有 CMake 和 C++ 单元测试，但缺少可单独调参的端到端压力脚本。直接把压力逻辑写进 C++ 测试会让 CTest 默认成本变高，也不便于调整压力规模。

**Decision**: 新增独立 Python 脚本，默认使用临时数据目录和已有可执行文件；需要时调用 CMake 构建目标。

**Consequences**: 脚本更灵活，适合人工或本地 AI 运行；但 HTTP 自动拉起受 `http_server` 在非 Windows 平台 daemon 化行为限制，因此非 Windows 默认只跑命令行入口，HTTP 可通过 `--http-url` 压测已运行服务。

## Out of Scope

* 不修改 `Searcher`、`Index` 或 HTTP 服务实现。
* 不新增第三方压测工具或包管理配置。
* 不把大规模压力测试加入默认 CTest，避免拖慢常规验证。
* 不做 CI 或远程环境验证。

## Technical Notes

* 复用模式 1：`engine/CMakeLists.txt` 是唯一构建入口，新增脚本通过 `cmake -S engine -B <build_dir>` 和 `cmake --build` 复用现有构建系统。
* 复用模式 2：`tests/searcher_tests.cc` 使用临时 raw 文件隔离测试数据，新脚本进一步用 `build/stress-work/` 隔离 `./data/raw.txt`。
* 复用模式 3：`debug` 的标准输入循环可用于同一进程内持续查询，避免反复重建索引。
* 复用模式 4：`http_server` 的 `/s?word=` 是真实 HTTP 搜索入口，可用于端到端并发压测。
* 兼容策略：旧构建产物可能仍读取 `../../data/raw.txt`，脚本会额外写入 `build/data/raw.txt`，该路径位于构建忽略目录内，下次运行可覆盖。
* 关键风险：非 Windows 平台 `http_server` 会 daemon 化，脚本不能安全回收进程，因此默认不自动启动，只支持压测传入的 `--http-url`。
* 性能边界：脚本生成的查询和语料规模可配置，默认值应适中，避免本地机器误触发过重负载。
