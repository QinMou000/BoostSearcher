# Directory Structure

> How frontend code is organized in this project.

---

## Overview

<!--
Document your project's frontend directory structure here.

Questions to answer:
- Where do components live?
- How are features/modules organized?
- Where are shared utilities?
- How are assets organized?
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

<!-- How should new features be organized? -->

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

## 场景：面向面试展示的静态技术说明页

### 1. 适用范围

* 新增只介绍项目既有技术原理的页面，例如索引、排序、召回等实现说明。
* 适用文件：`wwwroot/<page>.html` 和首页 `wwwroot/index.html` 的导航入口。
* 不适用场景：内容需要运行时数据、用户输入或写操作时，应另行设计后端接口契约。

### 2. 页面约定

* 页面使用独立 HTML 文件，文件名使用小写连字符或项目既有命名，例如 `project.html`。
* 首页入口和返回首页链接使用以 `/` 开头的绝对路径，避免当前页面路径影响跳转。
* 固定技术说明直接写在 HTML 内，不为展示用途额外修改 C++ HTTP 服务、CMake 或搜索测试。
* 固定数学公式优先使用原生 `<math display="block">`、`<mfrac>`、`<msub>` 等 MathML 标签，使分式、下标等按数学规则排版；不应把公式伪装成代码块、图片，也不为此引入新的 CDN 脚本。

### 3. 内容与交互契约

* 内容只能描述已在源码或 README 中证实的能力；每个技术点按“解决什么、怎样实现、为何取舍”组织。
* 面试展示页需要评估现状时，按“当前设计、当前不足、优化方向”分区，并明确标注优化方向尚未实现；优先使用连续标题、段落、列表和公式块，不默认使用卡片或图表。
* 不足和优化建议必须能回溯到源码或 README；例如全词典模糊扫描可关联 BK-tree、trigram 等候选索引方向。
* 复用 `boost-searcher-theme` 作为 `localStorage` 键，并沿用首页的浅色、深色主题切换行为。
* 页面不应请求新增接口，也不应把展示内容伪装成实时性能数据。

### 4. 验证要求

* 浏览器访问静态页应显示标题、检索链路和全部技术章节，控制台无警告或错误。
* 从首页入口进入页面应成功；主题切换后应正确更新页面主题。
* 页面不修改搜索核心逻辑时，仍需运行既有 `ctest --test-dir build -C Debug --output-on-failure`，确认回归通过。

### 5. 正反示例

#### 不推荐

```html
<script>
fetch("/api/tech-principles");
</script>
```

仅为固定面试说明新增接口，会增加后端代码和展示失败面。

#### 推荐

```html
<a href="/project.html">技术原理</a>
<h1>核心技术原理</h1>
```

固定说明随静态页面发布，导航路径不受当前页面位置影响。
