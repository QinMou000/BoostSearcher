# Boost Searcher

一个用 C++17 实现的本地 Markdown 文档搜索服务。

项目会把 `data/raw/md/` 下的 Markdown 文件解析成统一语料
`data/raw.txt`，启动时在内存中构建正排索引和倒排索引，并通过 HTTP
接口返回 JSON 搜索结果。当前版本支持中文分词、权重排序、摘要生成、
以 `data/` 为根目录的结果路径，以及一版基于编辑距离的模糊搜索。

## 功能概览

- Markdown 文档解析：读取 `data/raw/md/*.md`，抽取标题、正文和文件路径。
- 正排索引：按 `doc_id` 保存文档标题、正文、URL。
- 倒排索引：用 cppjieba 对标题和正文分词，建立 `word -> 文档列表` 映射。
- 权重排序：标题命中权重大于正文命中，多个查询词命中同一文档时累加权重。
- 模糊搜索：精确查不到某个查询词时，再用编辑距离从词典中补召回相似词。
- JSON 输出：搜索接口返回 `title`、`desc`、`url` 字段，前端可直接消费。
- HTTP 服务：`/s?word=关键词` 返回搜索结果，静态页面从 `wwwroot/` 提供。
- 测试目录：`tests/` 中包含搜索功能、模糊搜索、并发查询和性能指标测试。

## 目录结构

```text
.
├── engine/
│   ├── CMakeLists.txt          # CMake 构建入口
│   ├── include/
│   │   ├── index.hpp           # 正排索引、倒排索引、单例 Index
│   │   ├── searcher.hpp        # 搜索、模糊匹配、排序、摘要、JSON 输出
│   │   ├── util.hpp            # 文件工具、字符串切分、cppjieba 封装
│   │   ├── log.hpp             # 简单日志
│   │   └── daemon.hpp          # Linux 守护进程封装
│   ├── src/
│   │   ├── parser.cc           # Markdown -> data/raw.txt
│   │   ├── debug.cc            # 命令行搜索调试入口
│   │   └── http_server.cc      # HTTP 搜索服务
│   └── third/                  # cppjieba、httplib、nlohmann/json 等第三方头文件
├── tests/
│   ├── searcher_tests.cc       # 搜索模块测试
│   └── README.md               # 测试说明
├── wwwroot/
│   └── index.html              # 搜索页面
└── data/
    ├── raw/md/                 # Markdown 原始文档
    └── raw.txt                 # parser 生成的索引语料
```

`data/` 通常是本地数据目录，不建议把大量原始文档提交到仓库。

## 数据流程

离线解析：

```text
data/raw/md/*.md
        │
        ▼
engine/src/parser.cc
        │  解析 title / content / data 相对路径
        ▼
data/raw.txt
```

在线搜索：

```text
HTTP /s?word=关键词 或 debug 命令行输入
        │
        ▼
ns_searcher::Searcher
        │  分词、精确查倒排、必要时模糊召回
        ▼
ns_index::Index
        │  正排索引 + 倒排索引
        ▼
JSON 数组 [{ "title": "...", "desc": "...", "url": "data/..." }]
```

`data/raw.txt` 每行格式为：

```text
title\3content\3url\n
```

其中 `\3` 是字段分隔符。

## 模糊搜索策略

当前实现偏保守：

- 每个 query 词先做精确查找。
- 只有精确未命中时，才进入模糊搜索。
- 1 个字符不做模糊，避免误召回。
- 2 到 4 个字符允许编辑距离 1。
- 5 个及以上字符允许编辑距离 2。
- 每个未命中的 query 词最多扩展 5 个相似词。
- 模糊命中按 60% 权重合并，避免压过精确命中结果。

这版实现会遍历倒排词典计算编辑距离，适合当前项目先验证效果。
如果数据量继续变大，可以把候选召回升级为 BK-tree、trigram 索引或拼音/同音召回。

## 构建

要求：

- CMake 3.14+
- 支持 C++17 的编译器
- Windows/MSVC、Linux/g++ 或 clang++ 均可
- cppjieba、httplib、nlohmann/json 已放在 `engine/third/`

在项目根目录执行：

```bash
cmake -S engine -B build
cmake --build build --config Debug
```

Windows/MSVC 下常见产物：

```text
build/Debug/parser.exe
build/Debug/debug.exe
build/Debug/http_server.exe
build/Debug/searcher_tests.exe
```

Linux/macOS 单配置生成器下常见产物：

```text
build/parser
build/debug
build/http_server
build/searcher_tests
```

## 运行

以下命令都建议在项目根目录执行，因为程序使用 `./data/...` 和 `./wwwroot/...`
这类相对路径。

1. 生成语料：

```bash
# Windows/MSVC
./build/Debug/parser.exe

# Linux/macOS
./build/parser
```

输入目录：`./data/raw/md`

输出文件：`./data/raw.txt`

2. 命令行调试搜索：

```bash
# Windows/MSVC
./build/Debug/debug.exe

# Linux/macOS
./build/debug
```

启动后按提示输入查询词，程序会打印 JSON 搜索结果。

3. 启动 HTTP 服务：

```bash
# Windows/MSVC
./build/Debug/http_server.exe

# Linux/macOS
./build/http_server
```

默认监听：

```text
0.0.0.0:8080
```

搜索接口：

```text
GET /s?word=网络协议
```

返回示例：

```json
[
  {
    "title": "网络：网络层（IP协议）和数据链路层",
    "desc": "...",
    "url": "data/raw/md/网络：网络层（IP协议）和数据链路层.md"
  }
]
```

浏览器也可以访问：

```text
http://localhost:8080/
```

## 测试

测试统一放在 `tests/` 目录。

构建并运行测试：

```bash
cmake --build build --target searcher_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

当前 `searcher_tests` 覆盖：

- 精确搜索能命中目标文档。
- 错字查询能触发模糊召回。
- 单字查询不会误触发模糊召回。
- 返回结果中的 `url` 以 `data/` 为根目录。
- 多线程并发查询时结果保持稳定。
- 并发测试输出 QPS、总查询数、总耗时、平均延迟。

并发测试的性能输出示例：

```text
[METRIC] concurrent_search threads=8 total_queries=1200 elapsed_ms=... qps=... avg_latency_ms=...
```

这个 QPS 统计的是测试进程内 `Searcher::Search + JSON 解析校验`，
不是 HTTP 服务端到端 QPS。

## 同步 Gitee 文章

`tools/sync_gitee_posts.py` 可独立检查 Gitee 个人文章仓库，并真实同步本地缺失或已被远端修订的 Markdown。它只使用 Python 标准库，默认下载到 `data/raw/md/`：

```bash
python -B tools/sync_gitee_posts.py
```

可通过 `--output-dir` 指定任意输出目录；工具会创建不存在的目录，并原子更新远端内容已变化的同路径文章。工具使用 Gitee Git Blob SHA 比较内容，相同的文章不会重复下载：

```bash
python -B tools/sync_gitee_posts.py --output-dir D:\articles
```

工具优先下载 Gitee 原始 Markdown 地址；该地址受限时，会回退到同路径的 Gitee Contents API，并再次校验路径和 SHA，确保不会写入其他文件。

首次全量下载可用 `--workers 8` 增加并发数；日常同步默认使用 4 个工作线程，取值范围为 1 到 16：

```bash
python -B tools/sync_gitee_posts.py --output-dir D:\articles --workers 8
```

如需先查看本次会新增或更新哪些文件，显式传入 `--dry-run`：

```bash
python -B tools/sync_gitee_posts.py --dry-run --output-dir D:\articles
```

可用 `--timeout` 调整单个网络请求的超时秒数，用 `--max-new-files` 设置单次允许新增的文章数量上限。同步完成后，如需让新增文章进入现有索引，仍需单独运行 `parser` 生成 `data/raw.txt`，再重启 `http_server`。

离线自动化测试不会访问 Gitee，也不会写入项目真实语料：

```bash
python -B -m unittest discover -s tests -p "test_sync_gitee_posts.py" -v
```

## 当前限制

- 模糊搜索仍是全词典扫描，数据量大后需要更高效的候选索引。
- 测试性能指标目前只有 QPS 和平均延迟，还没有 P95/P99、Hit@K、MRR 等质量指标。
- HTTP 服务端到端压测尚未接入。
- `http_server` 在启动时一次性构建内存索引，暂不支持运行时增量更新文档。

## 技术栈

- C++17
- CMake
- cppjieba：中文分词
- nlohmann/json：JSON 构造与解析
- cpp-httplib：HTTP 服务
- 标准库 `filesystem`、`unordered_map`、`thread`、`atomic`
