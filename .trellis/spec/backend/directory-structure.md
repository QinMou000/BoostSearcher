# Directory Structure

> How backend code is organized in this project.

---

## Overview

<!--
Document your project's backend directory structure here.

Questions to answer:
- How are modules/packages organized?
- Where does business logic live?
- Where are API endpoints defined?
- How are utilities and helpers organized?
-->

(To be filled by the team)

---

## Directory Layout

```
<!-- Replace with your actual structure -->
src/
├── ...
└── ...
```

---

## Module Organization

<!-- How should new features/modules be organized? -->

(To be filled by the team)

---

## Naming Conventions

<!-- File and folder naming rules -->

(To be filled by the team)

---

## Examples

<!-- Link to well-organized modules as examples -->

(To be filled by the team)

---

## 场景：第一方 C++ 模块拆分

### 1. Scope / Trigger

* 触发条件：新增或修改 `engine/include/` 下第一方 C++ 模块，或新增需要被多个入口复用的后端实现。
* 适用范围：`engine/include`、`engine/src`、`engine/CMakeLists.txt`、`tests/searcher_tests.cc` 等本地搜索后端代码。
* 不适用范围：`engine/third` 下第三方依赖保持供应商原始结构，不重命名、不拆分。

### 2. Signatures

* 头文件路径：`engine/include/<module>.h`
* 实现文件路径：`engine/src/<module>.cpp`
* 入口文件路径：`engine/src/<entry>.cc`
* 公共库目标：`search_engine`

### 3. Contracts

* 第一方模块头文件只放声明、类型定义、必要模板和必须内联的小函数。
* 非模板函数、静态成员定义、静态对象定义必须放到唯一 `.cpp` 文件中。
* `LogMessage::operator<<` 这类模板函数必须保留在头文件中，避免调用点无法实例化。
* `Index::instance`、`Index::mtx`、`Jieba_util::jieba` 这类对象只能在一个 `.cpp` 中定义。
* `debug`、`http_server`、`parser`、`searcher_tests` 等目标应链接 `search_engine`，不得重新复制核心源文件列表。
* 入口 `.cc` 不能依赖核心头文件的间接 include；使用了 `std::cout`、`std::filesystem` 等能力时必须显式包含对应标准库头。

### 4. Validation & Error Matrix

* 代码中仍引用第一方 `.hpp` -> 失败，改为包含对应 `.h`。
* 静态对象定义仍留在头文件 -> 失败，移动到唯一 `.cpp`，避免多翻译单元重复定义。
* 新 `.cpp` 未加入 `search_engine` -> 失败，对应入口或测试会出现未解析外部符号。
* 入口依赖间接 include -> 失败，补充入口自身需要的标准库 include。
* CMake/MSBuild 因本地临时文件权限失败 -> 记录原因，并使用 `cl.exe` 直接编译入口和核心 `.cpp` 做补偿验证。

### 5. Good/Base/Bad Cases

* Good：`engine/include/searcher.h` 声明 `Searcher::Search`，`engine/src/searcher.cpp` 实现搜索、BM25、模糊召回和摘要逻辑。
* Base：`engine/src/debug.cc` 只包含入口交互逻辑，并显式包含 `<iostream>`。
* Bad：在 `engine/include/searcher.h` 继续写大段搜索实现，导致所有入口重复编译搜索算法。
* Bad：保留 `engine/include/util.hpp` 兼容包装层，造成新旧 include 规则并存。

### 6. Tests Required

* 必须运行 `git diff --check`。
* 必须扫描第一方旧后缀引用：`rg '"(daemon|index|log|searcher|util)\.hpp"|util\.hpp|index\.hpp|searcher\.hpp|log\.hpp|daemon\.hpp' engine tests`。
* 必须编译并运行 `searcher_tests`，验证搜索行为不因拆分改变。
* CMake 可用时优先运行 `cmake --build <build-dir> --target searcher_tests --config Debug`。
* CMake 不可用时，必须用 `cl.exe` 或等价编译器直接编译入口文件加核心 `.cpp`，至少覆盖 `searcher_tests`，并尽量覆盖 `parser`、`debug`、`http_server`。

### 7. Wrong vs Correct

#### Wrong

```cpp
// engine/include/index.h
Index *Index::instance = nullptr;
std::mutex Index::mtx;
```

#### Correct

```cpp
// engine/include/index.h
class Index {
    static Index *instance;
    static std::mutex mtx;
};

// engine/src/index.cpp
Index *Index::instance = nullptr;
std::mutex Index::mtx;
```
