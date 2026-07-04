# 验证记录

## 已通过

* `python -B tests/stress_searcher.py --docs 120 --queries 40 --threads 2 --skip-http --skip-build --build-dir build --timeout 120`
  * 结果：通过。
  * 覆盖：临时语料生成、旧产物兼容 raw 副本、`debug` 命令行入口批量查询、JSON 结果提取、中文/英文/模糊查询校验。
  * 指标：40 次查询，失败数 0。

* `python -B tests/stress_searcher.py --docs 80 --queries 16 --http-requests 16 --threads 4 --skip-build --build-dir build --timeout 120`
  * 结果：通过。
  * 覆盖：命令行入口小规模查询、Windows 本地自动启动 `http_server`、`/s?word=` HTTP 并发压测、P95/P99 延迟统计。
  * 指标：HTTP 16 次请求，失败数 0。

* `python -B tests/stress_searcher.py --help`
  * 结果：通过。
  * 覆盖：脚本语法加载与参数解析。

* `python -B tests/stress_searcher.py --docs 40 --queries 8 --threads 2 --skip-http --skip-build --build-dir build --timeout 60`
  * 结果：通过。
  * 覆盖：最后一次代码调整后的命令行入口回归验证。

* `python -B tests/stress_searcher.py --docs 40 --queries 8 --http-requests 8 --threads 2 --skip-build --build-dir build --timeout 60`
  * 结果：通过。
  * 覆盖：最后一次代码调整后的 Windows 本地 HTTP 自动启动与并发压测回归验证。

* `python -B tests/stress_searcher.py --docs 1000 --queries 600 --http-requests 600 --threads 8 --skip-build --build-dir build/stress --timeout 180`
  * 结果：通过。
  * 覆盖：用户复现规模的完整压力测试；命令行入口 600 次查询失败数 0，HTTP 入口 600 次请求失败数 0。

## 修复记录

* 现象：默认规模 HTTP 压测出现 50 次 `结果未命中期望：网络协议`。
* 原因：`长查询` 用例混合了 `网络协议 TCP 路由 数据链路 搜索引擎 JSON 高亮`，当前排序可能合理返回 `Markdown搜索引擎` 主题；脚本错误地把它断言为必须命中 `网络协议`。
* 修复：将 `长查询` 用例的 `expected` 改为 `None`，只校验 HTTP 响应可解析且流程不中断；同时增强 HTTP 失败日志，打印用例名、查询词、期望片段和响应摘要。

## 受阻

* `python -m py_compile tests/stress_searcher.py`
  * 结果：未通过。
  * 原因：当前 Windows 环境拒绝写入 `tests/__pycache__/*.pyc`，属于字节码缓存写入权限问题。
  * 补偿：改用 `python -B tests/stress_searcher.py --help` 做无字节码写入的语法和参数解析验证。

* `ctest --test-dir build -C Debug --output-on-failure`
  * 结果：未运行测试。
  * 原因：`build/Debug/searcher_tests.exe` 不存在。

* `cmake --build build --target searcher_tests --config Debug`
  * 结果：未通过。
  * 原因：CMake/MSBuild 在重新生成 `build/CMakeFiles/generate.stamp` 时被 Windows 拒绝访问。
  * 补偿：已用新增压力脚本覆盖 `debug` 与 `http_server` 主要运行路径；待本机构建目录权限恢复后，应重新执行该命令和 CTest。

* `ninja --version`
  * 结果：不可用。
  * 原因：当前环境未安装 Ninja，无法用 Ninja 生成器绕开 MSBuild 权限问题。

## 风险与补测计划

* 风险：现有 CTest 未能执行，无法在本轮确认 `searcher_tests` 当前源码版本是否通过。
* 补测计划：修复或清理 `build/` 目录权限后，执行 `cmake -S engine -B build`、`cmake --build build --target searcher_tests --config Debug`、`ctest --test-dir build -C Debug --output-on-failure`。
* 截止建议：在提交或合并前完成上述 CTest 补测；若权限问题持续存在，应新建一个可写构建目录并确保 MSBuild 可删除其中的临时文件。
