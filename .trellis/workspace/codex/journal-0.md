# Journal - codex (Part 0)

> AI development session journal
> Started: 2026-07-04

---



## Session 1: 新增搜索压力测试脚本

**Date**: 2026-07-04
**Task**: 新增搜索压力测试脚本
**Branch**: `master`

### Summary

新增 tests/stress_searcher.py，支持生成临时语料并压测 debug 与 HTTP 搜索入口；记录验证结果和后端压力测试规范。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `bd0a6a3` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 2: 实现 BM25 搜索排序

**Date**: 2026-07-04
**Task**: 实现 BM25 搜索排序
**Branch**: `master`

### Summary

为搜索后端引入字段化 BM25 排序，补充标题优先测试，并记录 backend 排序契约。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `46d0754` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 3: 拆分 C++ 头文件和实现文件

**Date**: 2026-07-04
**Task**: 拆分 C++ 头文件和实现文件
**Branch**: `master`

### Summary

将第一方 C++ 模块拆分为 .h/.cpp，新增 search_engine 静态库并同步后端目录结构规范。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `<上一步代码提交哈希>` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 4: 完善搜索技术说明页面

**Date**: 2026-08-15
**Task**: 完善搜索技术说明页面
**Branch**: `master`

### Summary

新增面向面试展示的静态技术说明页，说明搜索流程、BM25F 与编辑距离模糊召回；使用原生 MathML 渲染公式，并完成浏览器与 CTest 本地验证。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `004de13` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 5: 统一日志时间输出格式

**Date**: 2026-08-16
**Task**: 统一日志时间输出格式
**Branch**: `master`

### Summary

将统一日志时间从 Unix 时间戳改为本地 YYYY-MM-DD HH:MM:SS 格式，新增独立日志格式测试与 CMake 测试注册，并完成直接 MSVC 编译和既有 CTest 验证。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `74658c9` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
