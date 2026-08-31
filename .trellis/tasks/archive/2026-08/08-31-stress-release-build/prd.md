# 将搜索压力测试默认构建切换为 Release

## Goal

将 `tests/stress_searcher.py` 的默认构建配置从 Debug 切换为 Release，使无参数压力测试反映经过编译器优化的本地搜索性能，同时保留通过参数显式测试其他 CMake 配置的能力。

## Requirements

* 将 `--config` 的默认值由 `Debug` 改为 `Release`。
* 不传 `--config` 时，配置、构建和可执行文件定位均使用 `Release`。
* 显式传入 `--config Debug` 等配置名时，保持现有覆盖行为。
* 当 HTTP 压测已采集单请求延迟时，额外输出“实际平均响应延迟_ms”，其值为所有单请求耗时的算术平均值；保留原有“平均延迟_ms”字段以维持既有输出兼容性。
* 在相同默认语料、请求数和随机种子下，执行 Release 的 1、2、4、8、16 并发曲线，并基于 QPS、实际平均响应延迟、P95 与 P99 判断优化收益和饱和点。
* 不修改 C++ 搜索逻辑、CMake 目标、语料生成、HTTP 压测或临时目录隔离策略。

## Acceptance Criteria

* [ ] `python -B tests/stress_searcher.py --help` 显示 `--config` 默认值为 Release。
* [ ] 小规模无 `--config` 的完整压测成功，构建日志包含 `--config Release`，且实际使用 `build/stress/Release/debug.exe` 与 `http_server.exe`。
* [ ] HTTP 指标同时输出原有“平均延迟_ms”和由单请求耗时计算的“实际平均响应延迟_ms”。
* [ ] Release 1、2、4、8、16 并发压测均失败数为 0，并记录完整曲线数据和结论。
* [ ] `git diff --check` 通过。
* [ ] 显式传入 `--config Debug` 仍能解析参数，证明覆盖入口未被移除。

## Definition of Done

* 仅修改完成该默认值切换所必需的文件。
* 本地自动执行上述验证，不使用 CI、远程流水线或人工验证。
* 若构建或压测失败，记录原因并停止交付。

## Technical Approach

`argparse` 中的 `--config` 是构建命令和可执行文件路径的唯一配置来源。只修改该参数的默认值即可同时影响 CMake `--build --config` 和 Release 产物定位；用户仍可传入 `--config Debug` 覆盖默认值。

HTTP 压测已收集每个 future 返回的单请求耗时。指标汇总在延迟列表存在时计算其算术平均值并追加输出，不改变命令行入口的总耗时口径。并发曲线通过重复调用既有 `--threads` 参数执行，保持默认随机种子不变，避免引入第二套压测入口。

## Decision (ADR-lite)

**Context**：此前默认 Debug 使用 `/Od /RTC1`，不适合作为性能压测基线。

**Decision**：将无参数压测的默认配置切换为 Release，保留可选的 `--config` 覆盖；在 HTTP 指标中明确区分总耗时摊销值和单请求实际平均响应延迟，并用现有参数完成本地 Release 并发曲线验证。

**Consequences**：默认结果可用于分析优化后代码性能；调试构建测试需显式添加 `--config Debug`。实际平均值会揭示排队和并发竞争，不能再将总耗时除以请求数误读为单请求延迟。

## Out of Scope

* 不调整查询算法、线程数、日志策略或压力模型。
* 不新增自动遍历多个并发度的命令行参数；本轮曲线复用已有 `--threads`，避免扩大脚本接口。
* 不新增构建目录、CMake 预设、依赖或第三方压测工具。
* 不修改默认查询数、文档数和超时阈值。

## Technical Notes

* 已分析 `tests/stress_searcher.py`：`args.config` 同时传给 `cmake --build --config`、`find_executable` 和候选路径；单点默认值可满足要求。
* 已分析 `Metric.print_summary` 与 `run_http_stress`：HTTP 已传入完整 `latencies_ms` 列表，增加实际平均值无需改变请求、错误处理或 percentile 计算。
* 已分析 `engine/CMakeLists.txt`：`debug` 与 `http_server` 是既有 CMake 目标，不需要新增 Release 目标。
* 已分析 `tests/README.md` 与归档压力测试 PRD：脚本必须复用项目现有 CMake、保持临时语料隔离，并覆盖命令行和 HTTP 入口。
