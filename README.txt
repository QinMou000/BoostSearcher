Boost Searcher 中文说明
========================

一、项目简介
------------

Boost Searcher 是一个基于 C++14 的**本地文档搜索引擎**，用于对 Boost 官方 HTML 文档进行全文检索。  
项目包含从「文档解析 → 建立索引 → 搜索服务 → Web 前端展示」的完整链路，适合作为搜索引擎基础原理与 C++ 工程实践的简历项目。


二、整体架构（简易示意图）
--------------------------

数据离线处理流程：

    HTML 文档（data/input/*.html）
                │
                ▼
          parser.cc（解析）
                │   抽取 title / content / url
                ▼
      raw.txt（data/raw_html/raw.txt）
                │
                ▼
   index.hpp / searcher.hpp（建立正排 / 倒排索引）

在线搜索与服务流程：

    用户浏览器（wwwroot/index.html）
                │  Ajax: /s?word=关键字
                ▼
        http_server.cc（HTTP 服务）
                │ 调用
                ▼
      ns_searcher::Searcher（搜索逻辑）
                │  使用索引
                ▼
   ns_index::Index（正排 / 倒排索引）
                │  读取 raw.txt 构建
                ▼
        JSON 结果 → 前端渲染列表


三、主要目录与文件
------------------

- `parser.cc`           ：离线解析 Boost HTML，生成原始语料 `data/raw_html/raw.txt`。
- `index.hpp`           ：索引模块（正排索引、倒排索引、单例 Index）。
- `searcher.hpp`        ：搜索模块（分词、倒排合并、排序、摘要生成、JSON 输出）。
- `http_server.cc`      ：HTTP 服务主程序，提供 `/s?word=xxx` 搜索接口并托管静态页面。
- `debug.cc`            ：命令行调试入口，可以直接在终端输入查询词查看 JSON 结果。
- `util.hpp`            ：文件读写、字符串拆分、cppjieba 封装。
- `httplib.h`           ：cpp-httplib 单文件 HTTP 库。
- `log.hpp`             ：简单日志模块（后续可替换为 spdlog 等更完善方案）。
- `daemon.hpp`          ：将 HTTP 服务进程守护化（后台运行）。
- `mutex.hpp`           ：互斥锁封装（暂未大规模使用）。
- `Makefile`            ：项目编译脚本。
- `README.txt`          ：当前文档。

依赖与资源目录：

- `cppjieba/`           ：分词相关依赖（字典与头文件）。
  - `dict/`             ：分词所需的词典、模型文件。
  - `include/`          ：分词库头文件（cppjieba/、limonp/）。

- `data/`               ：数据目录（已在 `.gitignore` 中忽略）。
  - `input/`            ：原始 Boost HTML 文档。
  - `raw_html/`         ：解析后的原始语料文件 `raw.txt`。

- `wwwroot/`            ：Web 前端页面与静态资源。
  - `index.html`        ：搜索首页（jQuery + 简单响应式布局）。


四、编译与运行
--------------

1. 安装依赖（在本机或容器中提前安装）：

   - Boost（至少包含 `filesystem` 与 `algorithm/string`）
   - jsoncpp
   - pthread（Linux 环境）
   - cppjieba（本仓库已包含字典与头文件）

2. 在项目根目录执行编译：

   ```bash
   # MSCV
   mkdir build
   cd build
   cmake ../engine
   msbuild boost-searcher.sln /m /p:Configuration=Debug
   # msbuild boost-searcher.sln /m /p:Configuration=Release
   ```

   成功后会生成三个可执行文件：

   - `parser`
   - `debug`
   - `http_server`

3. 生成原始语料（离线步骤）：

   ```bash
   ./parser
   ```

   - 输入：`data/input/` 下的所有 `.html` 文件  
   - 输出：`data/raw_html/raw.txt`，每行格式为：

     `title\3content\3url\n`

4. 启动 HTTP 搜索服务：

   ```bash
   ./http_server
   ```

   - 默认监听：`0.0.0.0:8080`
   - 前端入口：浏览器访问 `http://localhost:8080/`

5. 调试模式（命令行查看 JSON 输出）：

   ```bash
   ./debug
   # 然后按提示在终端输入搜索关键词
   ```


五、技术栈说明
--------------

- 语言与标准：C++14
- 核心库：
  - Boost：文件系统遍历、字符串处理
  - cppjieba：中文分词（`CutForSearch` 模式）
  - jsoncpp：构造搜索结果 JSON
  - cpp-httplib：嵌入式 HTTP 服务器
  - pthread：HTTP 服务中多线程支持
- 前端：原生 HTML / CSS / JS + jQuery + Font Awesome
