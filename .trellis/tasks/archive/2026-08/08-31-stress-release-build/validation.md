# 本地验证记录

## 代码与帮助验证

* `python.exe -B .\tests\stress_searcher.py --help`
  * 通过：`--config` 帮助文本显示默认值为 `Release`。
* `python.exe -B .\tests\stress_searcher.py --config Debug --help`
  * 通过：显式配置参数仍可解析。
* `python.exe -B .\tests\stress_searcher.py --docs 120 --queries 40 --threads 1`
  * 通过：构建命令使用 `--config Release`，并实际执行 `build\stress\Release\debug.exe` 和 `http_server.exe`。
  * HTTP 指标同时输出 `平均延迟_ms=6.14` 和 `实际平均响应延迟_ms=6.11`，失败数为 0。

## Release 并发曲线

共同条件：1000 文档、600 命令行查询、600 HTTP 请求、固定默认随机种子；先完成 Release 构建，后续命令使用 `--skip-build` 复用同一产物。

| HTTP 并发 | QPS | 实际平均响应延迟_ms | P95_ms | P99_ms | 失败数 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 86.99 | 11.47 | 22.02 | 26.92 | 0 |
| 2 | 257.94 | 7.73 | 23.30 | 26.95 | 0 |
| 4 | 686.29 | 5.77 | 21.73 | 27.03 | 0 |
| 8 | 1512.81 | 5.21 | 8.09 | 14.99 | 0 |
| 16 | 1322.60 | 11.84 | 17.23 | 20.01 | 0 |

结论：8 并发是本轮最高吞吐点；升至 16 并发后 QPS 下降约 12.6%，实际平均响应延迟上升约 127.3%，表明该语料与机器条件下已经越过饱和点。与用户提供的 Debug 8 并发结果（16.26 QPS）相比，Release 的 8 并发结果为 1512.81 QPS；两者使用相同默认规模，但环境瞬态仍会影响绝对值，应以同机同语料的重复曲线判断后续优化收益。

## 回归测试

* `cmake --build build\stress --target searcher_tests --config Release`
  * 通过。
* `cmake --build build\stress --target log_tests --config Release`
  * 通过。
* `ctest --test-dir build\stress -C Release --output-on-failure`
  * 通过：`searcher_tests`、`log_tests` 共 2/2。
* `git diff --check`
  * 通过。

## 首次 CTest 处理

首次运行 CTest 时只构建了 `searcher_tests`，导致 `log_tests.exe` 不存在而无法执行；补建现有 `log_tests` 目标后重新运行，完整 CTest 已通过。该问题不涉及脚本行为或测试断言失败。
