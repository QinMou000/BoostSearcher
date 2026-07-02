# Tests

项目测试统一放在这个目录下。

当前测试入口：

- `searcher_tests.cc`：搜索模块测试，覆盖精确搜索、模糊搜索、单字不模糊召回、并发搜索，以及返回结果中的 `url` 是否保持 `data/` 根路径。

并发测试会额外输出一行性能参考数据：

```text
[METRIC] concurrent_search threads=8 total_queries=1200 elapsed_ms=... qps=... avg_latency_ms=...
```

这里的 QPS 统计范围是测试进程内的 `Searcher::Search` 调用加 JSON 解析校验成本，不等同于 HTTP 服务端到端 QPS。

运行方式：

```bash
cmake --build build --target searcher_tests
ctest --test-dir build -C Debug --output-on-failure
```
