# Quality Guidelines

> Code quality standards for backend development.

---

## Overview

<!--
Document your project's quality standards here.

Questions to answer:
- What patterns are forbidden?
- What linting rules do you enforce?
- What are your testing requirements?
- What code review standards apply?
-->

(To be filled by the team)

---

## Forbidden Patterns

<!-- Patterns that should never be used and why -->

(To be filled by the team)

---

## Required Patterns

<!-- Patterns that must always be used -->

- 新增函数和关键逻辑语句必须添加简洁中文注释。注释说明代码意图、
  安全约束或保护的行为，不要重复描述简单赋值。

---

## Testing Requirements

<!-- What level of testing is expected -->

## 场景：搜索压力测试脚本

### 1. Scope / Trigger

* 触发条件：新增或修改 `tests/` 下用于搜索引擎、命令行入口、HTTP 入口的本地压力测试脚本。
* 适用范围：生成临时语料、调用 CMake 构建产物、压测 `debug` 或 `http_server` 的脚本。

### 2. Signatures

* 标准命令：`python -B tests/stress_searcher.py [options]`
* 常用参数：
  * `--docs <int>`：生成文档数量，必须大于 0。
  * `--queries <int>`：命令行入口查询数量，必须大于 0。
  * `--threads <int>`：HTTP 并发数，必须大于 0。
  * `--build-dir <path>`：CMake 构建目录。
  * `--skip-build`：跳过构建，使用已有二进制。
  * `--skip-http`：只验证命令行入口。
  * `--http-url <url>`：压测已运行的 HTTP 服务。

### 3. Contracts

* 脚本必须使用项目现有 CMake 目标，不得引入第二套构建系统。
* 脚本不得写入或覆盖仓库根目录的 `data/raw.txt`。
* 临时语料应写入 `build/` 忽略目录或用户显式指定的工作目录。
* 成功退出码必须为 0；构建失败、二进制缺失、查询校验失败、HTTP 错误必须返回非零退出码。
* 指标输出至少包含查询数、耗时、QPS、平均延迟和失败数；HTTP 压测应额外输出尾延迟。

### 4. Validation & Error Matrix

* 参数小于等于 0 -> 立即失败并提示参数名。
* 找不到 CMake -> 失败并提示手动构建或使用 `--skip-build`。
* 找不到目标二进制 -> 失败并列出已检查路径。
* 搜索结果缺失期望内容 -> 失败并打印用例名、查询词和期望片段。
* 非 Windows 平台无法安全自动回收 daemon 化 HTTP 服务 -> 默认跳过本地自动启动，要求使用 `--http-url`。
* 混合多个主题的长查询 -> 只校验响应可解析和流程稳定，不断言固定主题排序。

### 5. Good/Base/Bad Cases

* Good：`python -B tests/stress_searcher.py --docs 1000 --queries 600 --threads 8`
* Base：`python -B tests/stress_searcher.py --docs 120 --queries 40 --skip-http --skip-build --build-dir build`
* Bad：脚本直接覆盖 `data/raw.txt`，或把大规模压力测试强行加入默认 CTest。
* Bad：对混合主题长查询硬编码单一主题期望，导致排序策略合理变化时出现误报。

### 6. Tests Required

* 参数解析：`python -B tests/stress_searcher.py --help` 必须成功。
* 命令行压测：小规模 `--skip-http` 运行必须失败数为 0。
* HTTP 压测：Windows 自动启动或 `--http-url` 模式必须覆盖 `/s?word=` 并校验失败数为 0。
* 既有 CTest：构建环境允许时必须执行 `ctest --test-dir build -C Debug --output-on-failure`。

### 7. Wrong vs Correct

#### Wrong

```bash
python tests/stress_searcher.py
# 脚本内部直接写 ./data/raw.txt，污染真实语料。
```

#### Correct

```bash
python -B tests/stress_searcher.py --docs 120 --queries 40 --skip-http
# 脚本内部使用 build/stress-work/.../data/raw.txt，并复用 CMake 构建产物。
```

---

## Code Review Checklist

<!-- What reviewers should check -->

(To be filled by the team)
