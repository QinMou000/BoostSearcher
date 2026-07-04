# 实现 BM25 搜索排序

## Goal

把当前基于固定整数权重累加的搜索排序升级为第一版 BM25 字段化排序，让标题、正文命中仍保持不同权重，同时引入 IDF、词频饱和和文档长度归一化，提升排序质量并为后续大语料优化打基础。

## Requirements

* 保持 `Searcher::Search(query, &json)` 对外接口不变。
* 保持返回 JSON 字段 `title`、`desc`、`url`、`keywords` 不变。
* 在索引阶段记录标题词频、正文词频、标题长度、正文长度、平均字段长度和文档总数。
* 在搜索阶段使用 BM25 计算标题和正文得分，并用字段权重合并。
* 保留现有短语命中加成、模糊召回降权和 top 100 截断逻辑。
* 不在本任务中实现 BK-tree、bigram 候选索引、持久化索引或增量更新。
* 尊重当前工作区中已有的 `searcher.hpp` 注释改动，不回滚用户已有修改。

## Acceptance Criteria

* [ ] 精确搜索仍能命中目标文档。
* [ ] 模糊搜索仍能召回目标文档。
* [ ] 单字查询仍不会误触发模糊召回。
* [ ] 并发搜索测试仍稳定通过。
* [ ] 排序实现不改变 HTTP 和 debug 入口调用方式。

## Definition of Done

* 本地构建或等价编译验证通过。
* `searcher_tests` 或可替代的直接编译测试通过。
* 若 CMake/MSBuild 环境因权限问题无法完整验证，需记录阻塞原因和补偿验证。

## Technical Approach

在 `Index` 层扩展倒排项和文档元数据：倒排项记录 `title_count`、`content_count`，文档记录标题/正文分词长度；构建完成后维护平均标题长度和平均正文长度。`Searcher` 层把原有 `int sum_weight` 改为 `double score`，对每个命中词按字段 BM25 计算得分，并在模糊召回时乘以 0.6 降权。

## Decision (ADR-lite)

**Context**: 当前排序只用固定权重累加，不能区分常见词和稀有词，也没有长文归一化。

**Decision**: 第一版采用 BM25F 的轻量形态：标题字段权重高于正文，字段内使用标准 BM25 公式。

**Consequences**: 排序分数变为浮点数，索引结构需要携带更多统计信息；但对外接口不变，测试和前端无需调整。

## Out of Scope

* 不替换 cpp-httplib。
* 不优化模糊搜索全词典扫描。
* 不做索引持久化。
* 不调整前端展示。

## Technical Notes

* 现有 `Index::BuildInvertedIndex` 已经分开统计标题和正文词频，是 BM25 字段化的主要复用点。
* 现有 `Searcher::Search` 已经集中处理倒排链合并、短语加权和 top 100 截断，适合在这里替换排序分数。
* 现有测试覆盖精确、模糊、单字、并发和 URL 根路径，应作为回归验证基础。
