# Gitee 文章同步接口调研

## 结论

可以使用公开的 Gitee V5 内容接口获取个人文章仓库，不需要网页登录或访问渲染后的博客页面。

## 已验证接口

1. 仓库元数据：
   `https://gitee.com/api/v5/repos/wang-qin928/personal_post`
   - 返回 HTTP 200。
   - 仓库公开，默认分支为 `main`。

2. 文章目录：
   `https://gitee.com/api/v5/repos/wang-qin928/personal_post/contents/source/_posts?ref=main`
   - 返回 HTTP 200。
- 当前返回 190 个文章文件条目。
- 条目包含 `type`、`path`、`sha`、`download_url` 字段。

3. 单篇原文：
   `download_url` 指向 `https://gitee.com/wang-qin928/personal_post/raw/main/source/_posts/<文件名>.md`。

## 对实现的约束

- 请求和响应统一按 UTF-8 处理，避免中文文件名被系统默认编码误解码。
- 只接受固定仓库、固定分支、固定目录、`.md` 扩展名与 HTTPS 下载地址。
- 目录接口可能返回非文件条目；工具必须过滤，而不是递归下载或执行任何远端内容。
- 本地当前有 185 个 Markdown 文件，远端目录有 190 个条目；数量差只用于试运行观察，最终更新判定以远端与本地的 Git Blob SHA 是否一致为准。
- 已验证远端 `2024年终总结.md` 的 `sha` 与本地文件经 `git hash-object` 计算出的 Git Blob SHA 一致，可作为无需下载正文的更新判定依据。
- Windows 本地工作区中的同一篇文章可能采用 CRLF，而仓库 Blob 采用 LF；实现比较前必须把本地 Markdown 的 CRLF 规范化为 LF，避免把纯换行差异误判为远端修订。

## 资料来源与用途

- Gitee 仓库 API：确认仓库公开性和默认分支。
- Gitee Contents API：确认目录枚举结构和原始 Markdown 下载地址。
