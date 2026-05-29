# Boost.Beast WebSocket 使用

Boost.Beast 提供了 HTTP 和 WebSocket 的 C++ 实现，基于 Boost.Asio。

## WebSocket 客户端

```cpp
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
namespace beast = boost::beast;
namespace websocket = beast::websocket;
```

### 建立连接

使用 `websocket::stream` 包装 TCP socket：

```cpp
websocket::stream<tcp::socket> ws{ioc};
ws.next_layer().connect(results);
ws.handshake(host, "/");
```

### 发送和接收消息

发送文本消息：

```cpp
ws.write(net::buffer("Hello, WebSocket!"));
```

接收消息：

```cpp
beast::flat_buffer buffer;
ws.read(buffer);
std::cout << beast::make_printable(buffer.data()) << std::endl;
```

## 错误处理

所有操作都通过 `boost::system::error_code` 传递错误。建议使用 `try-catch` 捕获异常。

> **提示**：在异步模式下，错误通过回调函数的参数传递。
