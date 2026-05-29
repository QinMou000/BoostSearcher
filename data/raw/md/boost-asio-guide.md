# Boost.Asio 入门指南

Boost.Asio 是一个跨平台的 C++ 库，用于网络和底层 I/O 编程。

## 概述

Asio 提供了**异步 I/O** 的能力，支持 TCP、UDP、SSL 等协议。它基于 `Proactor` 设计模式，通过事件循环驱动异步操作。

## 基本用法

首先包含头文件：

```cpp
#include <boost/asio.hpp>
using namespace boost::asio;
```

### 创建 io_context

`io_context` 是 Asio 的核心，管理所有异步操作：

```cpp
boost::asio::io_context io;
```

### TCP 客户端示例

使用 `tcp::socket` 连接服务器：

```cpp
tcp::socket socket(io);
tcp::resolver resolver(io);
boost::asio::connect(socket, resolver.resolve("example.com", "http"));
```

> **注意**：所有异步操作完成后需要调用 `io.run()` 来执行事件循环。

## 参考资料

- [官方文档](https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html)
- [GitHub 仓库](https://github.com/boostorg/asio)
