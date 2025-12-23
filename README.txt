Boost Searcher 项目说明
========================

项目简介
--------
本项目为基于 C++ 的文档搜索服务，支持分词、索引、HTTP 服务等功能，适用于本地文档检索和分布式搜索场景。

主要目录与文件结构
------------------

- `debug.cc`           ：调试相关代码。
- `http_server.cc`      ：HTTP 服务主程序。
- `httplib.h`           ：轻量级 HTTP 库头文件。
- `index.hpp`           ：索引模块头文件。
- `log.hpp`             ：日志模块头文件。
- `parser.cc`           ：文档解析主程序。
- `searcher.hpp`        ：搜索器模块头文件。
- `util.hpp`            ：工具函数头文件。
- `Makefile`            ：项目编译脚本。
- `README.txt`          ：项目说明文档。

- `cppjieba/`           ：分词相关依赖，包含字典和头文件。
	- `dict/`           ：分词所需的词典、模型文件。
	- `include/`        ：分词库头文件（cppjieba/、limonp/）。

- `data/`               ：数据目录。
	- `input/`          ：原始 HTML 文档及分类子目录。
	- `raw_html/`       ：原始 HTML 文本。

- `test/`               ：测试相关代码与资源。
	- `httplib.h`       ：测试用 HTTP 库。
	- `jieba_test.cpp`  ：分词功能测试。
	- `test.cc`         ：主测试程序。
	- `cppjieba/`       ：分词库测试资源。

- `wwwroot/`            ：Web 前端资源。
	- `index.html`      ：前端主页。

编译与运行
----------
1. 安装依赖 josn库 cppjieba库 boost开发库。
2. 使用 `make` 命令编译项目：
   ```bash
   make
   ```
3. 运行主程序（如 http_server）：
   ```bash
   ./http_server
   ```

更多说明
--------
- 分词依赖字典文件请参考 `cppjieba/dict/README.md`。
- 数据目录下 HTML 文件为待检索文档。
- 测试代码可用于验证分词和搜索功能。

如需详细用法或接口说明，请查阅各模块头文件或源码注释。