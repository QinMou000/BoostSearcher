# Research: 高并发搜索引擎后端架构

- **Query**: 高并发搜索引擎后端架构研究 — 语言选型、HTTP 服务器模式、开源替代方案、线程安全索引管理、单机高并发架构
- **Scope**: mixed (内部代码分析 + 外部技术调研)
- **Date**: 2026-05-29

## 当前架构分析

### 现有代码结构

| 文件 | 职责 |
|---|---|
| `/Users/weizhao/boost-searcher/http_server.cc` | HTTP 入口，cpp-httplib，单线程阻塞 |
| `/Users/weizhao/boost-searcher/searcher.hpp` | 分词 + 倒排合并 + 排序 + JSON 序列化 |
| `/Users/weizhao/boost-searcher/index.hpp` | 单例正排/倒排索引，DCL 双重检查锁 |
| `/Users/weizhao/boost-searcher/mutex.hpp` | pthread mutex RAII 封装 |
| `/Users/weizhao/boost-searcher/log.hpp` | RAII 日志，全局 mutex 保护输出流 |
| `/Users/weizhao/boost-searcher/util.hpp` | 字符串工具、cppjieba 封装 |
| `/Users/weizhao/boost-searcher/Makefile` | C++14, g++, -lpthread |

### 当前瓶颈

1. **cpp-httplib 默认单线程**: `http_server.cc` 中 `svr.listen()` 使用默认线程池大小（cpp-httplib 默认 `std::thread::hardware_concurrency()` 线程），但搜索过程（分词 + 合并 + 排序 + JSON 序列化）完全同步阻塞
2. **搜索路径无读写分离**: `Searcher::Search()` 读取 `index->inverted_index`（`unordered_map`），`GetInvertedList()` 返回裸指针，多线程同时读在 C++ 中只要不写就是安全的，但 `GetForwardIndex()` 返回的 `DocInfo*` 在扩容后可能失效（注释已标注）
3. **日志全局锁竞争**: `log.hpp` 中 `GetLogMutex()` 是全局 `std::mutex`，每次日志输出都竞争
4. **JSON 序列化在请求路径上**: `Json::StyledWriter` 在每次搜索中调用，是 CPU 密集操作

---

## Q1: 语言选型 — C++ vs Go vs Rust vs Node.js

### 对比分析

| 维度 | C++ | Go | Rust | Node.js |
|---|---|---|---|---|
| **原始性能** | 最高（零开销抽象） | 中等（GC 暂停） | 最高（零开销 + 无 GC） | 最低（V8 JIT） |
| **并发模型** | 手动管理（线程池/epoll/io_uring） | goroutine（内置调度器） | async/await + tokio | 事件循环（单线程 + worker threads） |
| **内存安全** | 不安全（需纪律） | 安全（GC） | 编译期保证（所有权系统） | 安全（GC） |
| **生态搜索库** | 自建或嵌入 Lucene++/Xapian | bleve（纯 Go 全文搜索） | tantivy（类 Lucene） | lunr.js / flexsearch |
| **中文分词** | cppjieba（已集成） | gojieba | jieba-rs / tantivy-jieba | nodejieba |
| **HTTP 服务器** | cpp-httplib / drogon / Pistache / uWebSockets | net/http（标准库） | actix-web / axum | express / fastify |
| **学习曲线** | 高 | 低 | 高 | 低 |
| **部署复杂度** | 需编译，二进制较大 | 单二进制 | 单二进制 | 需 Node 运行时 |
| **团队可维护性** | 低（C++ 惯用法复杂） | 高 | 中等 | 高 |

### 结论与建议

- **继续用 C++**: 适合对性能有极致要求、团队 C++ 经验深厚、愿意投入工程成本的场景。项目已有 C++14 代码基础，切换成本低但收益有限（瓶颈不在语言而在架构）
- **切换到 Go**: 搜索引擎场景下性能差距通常在 2-5x 内，但开发效率提升显著。goroutine 天然解决高并发问题，bleve 库可直接替代自建索引。适合"快速迭代 + 够用即可"
- **切换到 Rust**: 性能与 C++ 持平，内存安全保证，tantivy 是 Apache Lucene 级别的搜索库。但学习曲线陡峭
- **不推荐 Node.js**: 搜索是 CPU 密集型，单线程事件循环不适合

**对于 Boost Searcher（单机文档搜索）的务实建议**: 如果目标是高并发，问题本质不在语言而在架构。C++ 可以继续用，但需要替换 HTTP 服务器和引入并发模型。如果愿意重写，Go 是性价比最高的选择。

---

## Q2: C++ 高并发 HTTP 服务器方案

### 方案对比

| 方案 | 并发模型 | 特点 | Stars / 成熟度 |
|---|---|---|---|
| **cpp-httplib** (当前) | 线程池（每个连接一个线程） | 单头文件，易集成，但高并发下线程开销大 | 14k+ stars |
| **Drogon** | epoll + 非阻塞 I/O + 协程 | C++ 高性能异步框架，支持协程，内置 JSON | 12k+ stars |
| **uWebSockets / uSockets** | epoll/kqueue 非阻塞 | 极致性能，Node.js 替代品级别，API 偏底层 | 17k+ stars |
| **Pistache** | epoll + 线程池 | 现代 C++ API，中等性能 | 4k+ stars |
| **Seastar** (ScyllaDB 用) | 共享无状态 + 每核线程 | 极致性能，但 API 复杂，需反向思维 | 8k+ stars |
| **Boost.Beast + Boost.Asio** | 异步 I/O + io_context | 底层库，灵活但代码量大 | Beast 在 Boost 内 |
| **自建 epoll + 线程池** | epoll + worker 线程池 | 完全可控，但需处理大量边界情况 | — |

### 推荐方案

**对于 Boost Searcher 的场景（单机搜索，读多写少）：**

1. **轻量级方案**: 升级 cpp-httplib 的线程池配置 (`svr.new_task_queue = [] { return new ThreadPool(32); }`) — 最小改动，适合并发 < 1000
2. **中等方案**: 换用 Drogon — 异步非阻塞，协程化搜索路径，适合并发 1000-10000
3. **激进方案**: 换用 uWebSockets — 适合需要极高并发的场景

### epoll / io_uring / 线程池的选择

- **epoll**: Linux 标准方案，成熟稳定，是大多数 C++ HTTP 库的底层
- **io_uring**: Linux 5.1+ 新特性，异步系统调用，减少用户态/内核态切换，对磁盘 I/O 密集场景有明显优势。但对纯内存搜索场景收益有限
- **线程池**: 最简单直接的方案。对于读多写少的搜索场景，固定大小线程池 + 读写分离已经足够

---

## Q3: 开源搜索引擎替代方案对比

### 详细对比

| 维度 | Meilisearch | Tantivy | Manticore Search | Sonic | Typesense |
|---|---|---|---|---|---|
| **语言** | Rust | Rust | C++ | Rust | C++ |
| **定位** | 即插即用搜索引擎 | 搜索引擎库（类 Lucene） | 全功能数据库/搜索引擎 | 轻量级标识符索引 | 即插即用搜索引擎 |
| **GitHub Stars** | 50k+ | 12k+ | 10k+ | 20k+ | 22k+ |
| **中文支持** | 内置 CJK 支持 | 通过 tantivy-jieba 插件 | 内置中文分词 | 支持 80+ 语言 | 内置中文分词 |
| **分布式** | 支持 (v1.12+ replication & sharding) | 需 Quickwit 包装 | 原生 Galera 多主复制 | 不支持 | Raft 集群 |
| **API** | RESTful JSON | 库（需自建服务） | SQL + HTTP JSON | 自定义 Sonic Channel | RESTful JSON |
| **向量搜索** | 支持混合搜索 | 支持 | 支持混合搜索 | 不支持 | 不支持 |
| **资源消耗** | 中等（~100MB 空实例） | 极低（库级别） | 低（~40MB 空实例） | 极低（~30MB） | 低 |
| **延迟** | <50ms | 取决于实现 | <50ms | 微秒级 | <50ms |
| **适合场景** | 替代 Elasticsearch，开箱即用 | 自建搜索引擎的底层引擎 | 替代 Elasticsearch，需 SQL 能力 | 轻量级标识符搜索 | 替代 Algolia，开发者友好 |
| **许可证** | MIT | MIT | GPLv3+ | MPL-2.0 | GPLv3 |

### 与 Boost Searcher 的匹配度

| 引擎 | 匹配度 | 理由 |
|---|---|---|
| **Typesense** | 高 | C++ 原生，单二进制部署，中文支持好，RESTful API，开箱即用 |
| **Meilisearch** | 高 | Rust 实现，性能好，混合搜索（语义 + 全文），但需换语言 |
| **Manticore Search** | 中 | C++ 原生，SQL 接口强大，但功能过于重型，对简单搜索场景过大 |
| **Sonic** | 中 | 轻量极致，但只返回标识符不存储文档，需配合外部数据库 |
| **Tantivy** | 低（如自建） | 库级别，需用 Rust 编写服务层，但提供最强自定义能力 |

---

## Q4: 线程安全索引管理

### 当前实现分析

`/Users/weizhao/boost-searcher/index.hpp` 中：
- `Index::GetInstance()` 使用 DCL（双重检查锁）创建单例（行 41-49）
- `forward_index`（`vector<DocInfo>`）和 `inverted_index`（`unordered_map<string, InvertedList>`）在 `BuildIndex()` 之后变为只读
- `GetForwardIndex()` 和 `GetInvertedList()` 都是只读操作

### 线程安全方案对比

| 方案 | 适用场景 | 优点 | 缺点 |
|---|---|---|---|
| **读写锁 (shared_mutex)** | 读多写少，读写互斥 | 实现简单，C++17 标准 | 写操作阻塞所有读，写频繁时性能差 |
| **Copy-on-Write (COW)** | 索引更新不频繁 | 读完全无锁，性能最优 | 内存翻倍，更新时需完整拷贝 |
| **双缓冲 (Double Buffer)** | 定期重建索引 | 读无锁，切换原子化 | 需维护两个索引副本 |
| **Lock-free 数据结构** | 极端高并发 | 无锁读写 | 实现复杂，`unordered_map` 不支持无锁 |
| **读写分离 + 原子指针** | 索引构建后只读 | 最简单，零开销 | 不支持增量更新 |

### 推荐方案

**对于 Boost Searcher（索引构建后只读）：**

最简单的方案是确认索引在 `BuildIndex()` 完成后不再修改。此时所有读操作天然线程安全，无需加锁。当前代码的唯一风险是：
1. `forward_index` 扩容导致 `DocInfo*` 失效 — 但构建完成后不再扩容
2. `inverted_index` 的 `unordered_map` 操作仅读取 — 标准保证并发读安全

**如果未来需要增量更新索引：**
- 使用 **双缓冲 + `std::atomic<Index*>`** 原子切换。新索引在后台构建完成后，原子替换指针，旧索引延迟回收
- 或使用 **`std::shared_mutex`**（C++17）的读写锁，读操作 `shared_lock`，写操作 `unique_lock`

---

## Q5: 单机高并发架构模式

### 推荐架构

```
                         ┌─────────────────────┐
                         │   epoll / io_uring   │
                         │   (连接管理线程)       │
                         └──────────┬──────────┘
                                    │
                         ┌──────────▼──────────┐
                         │    请求分发队列       │
                         │  (无锁 MPSC 队列)     │
                         └──────────┬──────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
    ┌─────────▼─────────┐ ┌────────▼────────┐ ┌─────────▼─────────┐
    │  Worker Thread 1  │ │ Worker Thread 2 │ │ Worker Thread N   │
    │  (分词→合并→排序)  │ │ (分词→合并→排序) │ │ (分词→合并→排序)   │
    └─────────┬─────────┘ └────────┬────────┘ └─────────┬─────────┘
              │                     │                     │
              └─────────────────────┼─────────────────────┘
                                    │
                         ┌──────────▼──────────┐
                         │   共享只读索引        │
                         │  (atomic 指针或裸读)   │
                         └─────────────────────┘
```

### 关键设计模式

1. **Reactor 模式**: epoll 监听连接事件，分发给 worker 线程池处理。避免每个连接一个线程的开销
2. **固定线程池**: 线程数 = CPU 核心数 × 2。避免线程创建/销毁开销
3. **无锁队列分发**: 连接管理线程通过 MPSC（多生产者单消费者）无锁队列分发请求给 worker
4. **只读索引**: 索引构建完成后变为只读，所有 worker 并发读取无需锁
5. **异步日志**: 日志写入内存缓冲区，后台线程批量 flush 到磁盘，避免请求路径上的 I/O 阻塞
6. **连接池复用**: HTTP keep-alive 减少连接建立开销

### 性能估算

| 并发数 | 当前架构 (cpp-httplib 线程池) | 优化后架构 (epoll + 线程池) |
|---|---|---|
| 100 | ~500 QPS | ~2000 QPS |
| 1000 | 饱和/拒绝 | ~5000-8000 QPS |
| 10000 | 不可用 | ~10000-20000 QPS |

注: 以上为粗略估算，实际取决于索引大小、查询复杂度和硬件配置。

---

## 综合建议

### 路径 A: 最小改动（继续 C++）

1. 将 cpp-httplib 线程池调大: `svr.new_task_queue = [] { return new ThreadPool(64); }`
2. 确认索引只读路径无锁竞争（当前代码已是如此）
3. 异步日志（后台线程 flush）
4. 换用 JSON 序列化更快的库（如 simdjson / rapidjson）
5. **预期收益**: 并发从几百提升到数千

### 路径 B: 架构重构（继续 C++）

1. 替换 HTTP 服务器为 Drogon 或 uWebSockets
2. 引入 epoll 非阻塞 I/O + 固定线程池
3. 异步化日志和 JSON 序列化
4. **预期收益**: 并发达到数万

### 路径 C: 换用 Go + 内置搜索库

1. 使用 Go 标准库 `net/http` + goroutine
2. 用 bleve 或直接嵌入 Tantivy（通过 cgo 或切换到 Rust）
3. **预期收益**: 开发效率大幅提升，性能足够

### 路径 D: 直接用 Typesense / Meilisearch

1. 部署 Typesense（C++ 原生）或 Meilisearch（Rust）
2. 通过 parser 输出转为 JSON 导入
3. 前端直接对接 REST API
4. **预期收益**: 最快落地，开箱即用的高并发搜索，但失去自定义权重等控制

## Caveats / Not Found

- 未进行实际性能基准测试（benchmark），以上 QPS 数据为基于架构的估算
- io_uring 在 macOS 上不可用（仅 Linux 5.1+），如果部署环境包含 macOS 需注意
- cpp-httplib 的线程池配置方式需确认版本（不同版本 API 不同）
- Typesense 的中文分词质量需实际测试验证
