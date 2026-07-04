# 拆分 C++ 头文件和实现文件

## Goal

将项目内第一方 C++ 代码从当前的头文件集中实现，调整为声明放在 `.h`、实现放在 `.cpp` 的结构，降低头文件膨胀、重复编译和静态对象定义风险，为后续搜索引擎后端继续增长打基础。

## What I already know

* 用户明确要求：当前项目 C++ 代码都用 `.hpp`，随着代码增长，需要拆成 `.h` 和 `.cpp` 文件，并开始实施。
* 当前第一方头文件集中在 `engine/include/daemon.hpp`、`engine/include/index.hpp`、`engine/include/log.hpp`、`engine/include/searcher.hpp`、`engine/include/util.hpp`。
* 当前可执行入口集中在 `engine/src/debug.cc`、`engine/src/http_server.cc`、`engine/src/parser.cc`；测试入口是 `tests/searcher_tests.cc`。
* 当前 CMake 只为 `parser`、`debug`、`http_server`、`searcher_tests` 分别编译入口源文件，核心搜索逻辑依赖头文件内联实现，没有独立 engine 库目标。
* `index.hpp` 内含 `Index::instance` 和 `Index::mtx` 的静态成员定义，`util.hpp` 内含 `Jieba_util::jieba` 静态对象定义，继续放在头文件里会提高多翻译单元链接时的重复定义风险。
* `searcher.hpp` 目前包含大量私有算法实现，例如模糊召回、UTF-8 切分、BM25 算分、摘要生成和 JSON 输出，最适合优先拆分。
* `log.hpp` 当前多为 `inline` 函数和模板操作符，拆分时要保留模板定义在头文件中，非模板函数可迁移到 `.cpp`。
* 第三方目录 `engine/third/` 下的 `.hpp` 与 `httplib.h` 属于外部依赖，不应纳入本次重命名或拆分。

## Assumptions

* 本任务只处理项目第一方 C++ 代码，不重命名或修改第三方库文件。
* 对外行为保持不变：命令行入口、HTTP `/s?word=` 和 `/doc` 接口、测试查询行为都不改变。
* 优先建立一个 `search_engine` 之类的内部静态库目标，让 `debug`、`http_server`、`searcher_tests` 复用同一批 `.cpp` 实现。
* `parser.cc` 已经是独立实现文件，本任务只在必要时为其共用工具适配新的 `.h` include，不把 parser 逻辑拆成额外模块。

## Open Questions

* 已确认：采用“第一方头文件全量拆分，第三方文件保持不动”的方案。

## Requirements

* 将第一方 `.hpp` 迁移为 `.h` 声明文件，并把非模板、非必须内联实现移动到对应 `.cpp`。
* 更新所有 include，使项目代码引用新的 `.h` 文件。
* 更新 CMake，使可执行文件和测试链接同一套核心实现文件，避免重复编译和重复定义。
* 保持现有命名空间、类名、函数名、宏名和外部行为不变。
* 保留必要的中文注释，尤其是拆分后声明和实现之间容易产生误用的位置。

## Acceptance Criteria

* [ ] `engine/include` 中第一方核心头文件使用 `.h` 后缀。
* [ ] 搜索、索引、工具、日志、守护进程的非模板实现位于 `engine/src` 下对应 `.cpp` 文件。
* [ ] `debug`、`http_server`、`searcher_tests` 能链接拆分后的核心实现。
* [ ] `Searcher::Search` 对外接口和返回 JSON 字段保持不变。
* [ ] `searcher_tests` 覆盖精确搜索、模糊召回、单字边界、BM25 标题优先和并发搜索，并保持通过。
* [ ] 若 CMake/MSBuild 在本机因锁或权限失败，记录原因并提供等价 `cl.exe` 补偿验证。

## Definition of Done

* 本地编译或等价手工编译通过。
* 自动化测试通过，至少包含 `searcher_tests`。
* 工作区只包含本任务相关改动，未识别临时文件不提交。
* 如拆分形成新的目录或构建约定，更新 `.trellis/spec/backend` 中对应规范。
* 完成后按 Trellis 流程执行质量检查、规格更新、提交和收尾。

## Technical Approach

第一版采用“全量第一方拆分”：保留 `engine/include` 存放 `.h` 声明，新增或补齐 `engine/src/log.cpp`、`engine/src/util.cpp`、`engine/src/index.cpp`、`engine/src/searcher.cpp`、`engine/src/daemon.cpp` 存放实现。CMake 新增内部静态库目标并被 `debug`、`http_server`、`searcher_tests` 链接；`parser` 如只依赖日志和工具，也链接该库或对应公共实现。

## Decision (ADR-lite)

**Context**: 当前 header-only 写法让搜索算法、索引构建、日志策略和分词工具都暴露在 include 层，代码增长后会导致编译变慢、声明/实现边界模糊，并增加静态对象在多个翻译单元中重复定义的风险。

**Decision**: 用户选择方案 1：拆分第一方 `.hpp` 为 `.h + .cpp`，不触碰第三方依赖。

**Consequences**: 需要一次性更新 include、CMake 和直接编译测试命令；短期改动面较大，但后续新增实现可以稳定落到 `.cpp`，头文件只表达接口。

## Out of Scope

* 不改写搜索算法、BM25 参数、模糊召回策略或 HTTP 行为。
* 不替换 cpp-httplib、cppjieba、nlohmann/json 等第三方依赖。
* 不拆分第三方 `.hpp`。
* 不做跨平台安装包、动态库导出或 ABI 兼容设计。

## Technical Notes

* 主要影响文件：`engine/include/*.hpp`、`engine/src/*.cc`、`engine/CMakeLists.txt`、`tests/searcher_tests.cc`。
* 当前测试可通过直接编译 `tests/searcher_tests.cc` 验证，但拆分后需要把新 `.cpp` 一并参与链接。
* 需要特别处理模板函数：`LogMessage::operator<<` 这类模板必须继续留在头文件，否则调用点无法实例化。
* 需要特别处理静态成员和静态对象：`Index::instance`、`Index::mtx`、`Jieba_util::jieba` 应移动到唯一 `.cpp` 定义。
* 本机 CMake Visual Studio 生成器和 NMake 生成器都在 `try_compile` 清理临时文件时出现“拒绝访问”，因此本任务使用 `cl.exe` 直接编译所有拆分后的 `.cpp` 与入口文件作为补偿验证。
