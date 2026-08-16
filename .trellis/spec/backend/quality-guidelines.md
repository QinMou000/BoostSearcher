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

## 场景：搜索 BM25 排序

### 1. Scope / Trigger

* 触发条件：修改 `engine/include/index.hpp` 的倒排索引结构，或修改 `engine/include/searcher.hpp` 的搜索排序、短语加权、模糊召回降权逻辑。
* 适用范围：本地搜索后端的索引构建、命令行搜索入口和 HTTP `/s?word=` 入口共享同一套 `Searcher::Search` 排序逻辑。

### 2. Signatures

* 对外接口保持 `void Searcher::Search(const std::string &query, std::string *json)`。
* 返回 JSON 字段保持 `title`、`desc`、`url`、`keywords`。
* 索引层需要能提供文档总数、标题平均分词长度、正文平均分词长度，供搜索阶段计算 IDF 和长度归一化。

### 3. Contracts

* 倒排项必须记录同一词在标题和正文里的独立词频，不再只保存静态总权重。
* 文档正排信息必须记录标题分词长度和正文分词长度；每次重新构建索引前必须清空旧索引和旧统计。
* 搜索排序分数使用浮点数：标题 BM25 分和正文 BM25 分分别计算，再按字段权重合并。
* 短语命中加成必须与 BM25 分数保持同一量级，禁止使用会完全盖过 BM25 的超大常量。
* 模糊召回仍然只在精确未命中后触发，并继续使用低于精确命中的权重比例。

### 4. Validation & Error Matrix

* 空查询或只剩停用词 -> 返回空 JSON 数组。
* 单字查询未精确命中 -> 不触发模糊召回，避免误召回。
* 文档字段长度平均值为 0 -> BM25 字段分返回 0，避免除零。
* 倒排项中的 `doc_id` 越界 -> 跳过该项并保留 warning 日志。

### 5. Good/Base/Bad Cases

* Good：查询词同时出现在标题和正文时，标题命中文档应优先于只靠正文重复堆词的文档。
* Base：精确搜索、模糊搜索、无结果搜索和并发搜索行为保持稳定。
* Bad：把标题词频乘固定常量后直接累加为整数分，无法体现 IDF、词频饱和和文档长度归一化。
* Bad：把短语加成设置成数千或数万，导致 BM25 算分基本失效。

### 6. Tests Required

* 必须运行 `searcher_tests` 或等价手工编译测试，覆盖精确命中、模糊召回、单字无模糊、并发搜索和 BM25 标题优先排序。
* 构建环境允许时优先运行 `cmake --build <build-dir> --target searcher_tests --config Debug`。
* 若 MSBuild/CMake 因本地权限或锁文件失败，必须记录失败原因，并用 `cl.exe` 直接编译 `tests/searcher_tests.cc` 作为补偿验证。

### 7. Wrong vs Correct

#### Wrong

```cpp
elem.weight = title_count * 10 + content_count;
merged.sum_weight += elem.weight;
```

#### Correct

```cpp
elem.title_count = title_count;
elem.content_count = content_count;
merged.score += GetBm25Score(elem, doc, idf);
```

---

## 场景：独立 Gitee Markdown 同步工具

### 1. 范围与触发条件

* 触发条件：新增或修改 `tools/sync_gitee_posts.py`，或调整 Gitee 文章同步、落盘和定时调用前的验证流程。
* 适用范围：Gitee `wang-qin928/personal_post` 的 `main/source/_posts`、本地 Markdown 输出目录和 `tests/test_sync_gitee_posts.py`。
* 不适用范围：`http_server` 的凌晨调度、`parser` 重建 `data/raw.txt` 和运行中索引热更新需作为独立需求处理。

### 2. 签名

* 标准命令：`python -B tools/sync_gitee_posts.py [--output-dir <目录>] [--dry-run] [--workers <1-16>] [--timeout <秒>] [--max-new-files <数量>]`。
* 默认行为：真实同步到 `<仓库>/data/raw/md`；`--output-dir` 指定其他输出目录；仅 `--dry-run` 禁止下载和写入。
* 核心接口：`sync_posts(target_directory, apply, timeout_seconds, max_new_files, workers, fetch_bytes, print_line)` 返回 `SyncStats`。

### 3. 契约

* 远端目录条目必须是 `source/_posts` 直接子目录中的 `.md` 文件，并包含 40 位小写 Git Blob SHA 与白名单下载地址。
* 本地 Markdown 在比较 SHA 前必须将 CRLF 规范化为 LF；内容相同不下载，远端 SHA 不同则以远端版本为权威原子更新。
* 原始 Markdown 地址优先；发生请求错误时，只能回退到同一仓库、分支、路径和 SHA 的 Gitee Contents API Base64 正文。
* 文件必须先完整写入输出目录内的临时文件；新增使用同目录硬链接发布，更新使用 `os.replace`，不得暴露半写入文章。
* 下载并发默认 4，限制在 1 到 16；主线程按远端路径稳定输出结果，任务日志可直接比较。

### 4. 验证与错误矩阵

* `--dry-run` -> 只访问目录接口，输出新增和更新计划，不创建输出目录或文章文件。
* 输出文件缺失 -> 下载并原子新增。
* 输出文件存在且规范化 SHA 相同 -> 标记已同步，不请求正文。
* 输出文件存在且 SHA 不同 -> 下载并原子更新。
* 原始下载 451、超时或网络错误 -> 请求同路径 Contents API；备用接口路径、SHA、编码或 Base64 不合法 -> 本篇失败且进程返回非零。
* 路径回退、非 Markdown、非 HTTPS/白名单地址、正文超过 5 MiB、并发数不在 1 到 16 -> 拒绝并返回 `SyncError`。

### 5. 正常、基础与错误案例

* 正常：`python -B tools/sync_gitee_posts.py --output-dir D:\\articles --workers 8`，将缺失文章新增、将远端修订文章更新。
* 基础：`python -B tools/sync_gitee_posts.py --dry-run --output-dir D:\\articles`，仅确认本次计划。
* 错误：仅通过 `Path.exists()` 判断文章已同步，导致远端修订无法更新。
* 错误：原始地址失败后接受任意第三方下载 URL，绕过仓库和 SHA 校验。

### 6. 必需测试

* `python -B -m unittest discover -s tests -p "test_sync_gitee_posts.py" -v` 必须覆盖预览、首次新增、重复跳过、远端更新、CRLF/LF 比较、下载失败、原始地址备用回退、路径/URL/大小/并发边界。
* 真实冒烟测试必须使用独立 `--output-dir`，确认首次同步的 Markdown 数量与远端目录一致、临时文件数为零；第二次执行应没有新增或更新。
* 修改脚本后必须运行 `git diff --check`，并搜索废弃参数 `--apply`、`--target-dir` 和 `args.target_dir`。

### 7. 错误与正确实现

#### 错误

```python
if destination.exists():
    return "已同步"
```

#### 正确

```python
if destination.exists() and local_git_blob_sha(destination) == post.blob_sha:
    return "已同步"

content = fetch_post_content(post, timeout_seconds, fetch_bytes)
return publish_file(destination, content, post.blob_sha)
```

<!-- What reviewers should check -->

(To be filled by the team)
