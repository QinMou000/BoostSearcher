# 隐藏技术说明页可见滚动条

## 目标

隐藏 `wwwroot/project.html` 右侧及公式区域的可见滚动条，使面试展示页更干净，同时保留滚轮、触摸和键盘的正常滚动能力。

## 已知信息

* 用户截图显示 BM25 公式右侧有可见滚动条。
* 技术说明页使用长文档布局，页面必须继续可以纵向滚动。
* `.formula` 当前使用 `overflow-x: auto` 处理长公式。

## 需求

* 仅修改 `wwwroot/project.html` 的样式。
* 隐藏页面级和公式容器级的可见滚动条。
* 公式在窄屏时仍可横向滚动，不裁切内容。
* 不修改 C++、接口或其他页面。

## 验收标准

* [ ] 页面右侧不显示纵向滚动条。
* [ ] BM25 公式区域不显示滚动条。
* [ ] 长页面仍可通过程序化滚动和滚轮/键盘语义滚动。
* [ ] 窄屏公式容器仍具有横向溢出能力。
* [ ] 现有 CTest 通过。

## 技术方案

使用跨浏览器 CSS：Firefox 使用 `scrollbar-width: none`，旧版 Edge 使用 `-ms-overflow-style: none`，WebKit 内核使用 `::-webkit-scrollbar { display: none; }`。公式容器单独设置 `overflow-y: hidden`，避免 `overflow-x: auto` 连带产生纵向滚动条。

## 范围外

* 不禁用滚动行为。
* 不调整公式内容、字体或页面其他视觉设计。
