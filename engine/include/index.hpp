#pragma once
#include "log.hpp"
#include "util.hpp"
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ns_index {

struct DocInfo {
    std::string title;
    std::string content;
    std::string url;
    uint64_t doc_id;
    // title_len/content_len 记录的是分词后的 token 数，不是字节数或字符数。
    // BM25 的长度归一化按“词项数量”衡量字段长短，避免长正文仅靠堆词获得过高分。
    size_t title_len = 0;
    size_t content_len = 0;
};

struct InvertedElem {
    uint64_t doc_id;
    std::string word;
    // 倒排项保存字段内原始词频，搜索阶段再结合 IDF、字段长度和字段权重动态算分。
    // 不再提前折算成一个静态 weight，避免丢失标题/正文的独立统计信息。
    int title_count = 0;
    int content_count = 0;
};

typedef std::vector<InvertedElem> InvertedList;

class Index {
  private:
    std::vector<DocInfo> forward_index;                           // 正排索引，用数组下标作为文档 id
    std::unordered_map<std::string, InvertedList> inverted_index; // 倒排索引，关键字到倒排拉链的映射
    // 以下字段是 BM25 的集合级统计：先累加所有文档的字段长度，构建结束后再计算平均值。
    // 它们只随 BuildIndex 刷新，搜索阶段只读，因此并发查询不会修改这些状态。
    size_t total_title_len = 0;
    size_t total_content_len = 0;
    double avg_title_len = 0.0;
    double avg_content_len = 0.0;

    Index() {}
    Index(const Index &) = delete;
    Index &operator=(const Index &) = delete;

    static Index *instance;
    static std::mutex mtx;

  public:
    static Index *GetInstance() {
        if (instance == nullptr) {
            std::lock_guard<std::mutex> lock(mtx);
            if (instance == nullptr) {
                instance = new Index;
            }
        }
        return instance;
    }

    ~Index() {}

    DocInfo *GetForwardIndex(uint64_t doc_id) {
        if (doc_id >= forward_index.size()) {
            LOG(LogLevel::WARNING) << "doc_id out of range";
            return nullptr;
        }
        return &forward_index[doc_id];
    }

    /**
     * @brief 静默查找某个词对应的倒排拉链
     *
     * Searcher 在执行模糊搜索时，精确未命中是正常分支，不应该每次都打印
     * warning；因此这里单独提供一个不打日志的查找接口。
     */
    InvertedList *FindInvertedList(const std::string &word) {
        auto ret = inverted_index.find(word);
        if (ret == inverted_index.end()) {
            return nullptr;
        }
        return &ret->second;
    }

    /**
     * @brief 查找倒排拉链，找不到时记录 warning
     *
     * 保留原来的带日志接口，方便调试真正“不应该缺失”的调用场景。
     */
    InvertedList *GetInvertedList(std::string &word) {
        InvertedList *list = FindInvertedList(word);
        if (list == nullptr) {
            LOG(LogLevel::WARNING) << "can't find word: " + word + " in inverted_index";
            return nullptr;
        }
        return list;
    }

    /**
     * @brief 暴露完整倒排词典的只读视图
     *
     * 第一版模糊搜索直接遍历词典做候选召回。返回 const 引用可以避免拷贝
     * 整个倒排索引，同时禁止调用方修改索引内容。
     */
    const std::unordered_map<std::string, InvertedList> &GetInvertedIndex() const { return inverted_index; }

    // 搜索阶段只读取这些全局统计，不暴露可变索引内部结构；调用方无法绕过 Index 修改统计值。
    size_t GetDocCount() const { return forward_index.size(); }

    double GetAvgTitleLen() const { return avg_title_len; }

    double GetAvgContentLen() const { return avg_content_len; }

    /**
     * @brief 从 raw 文件构建正排和倒排索引
     * @param raw_file_path raw.txt 文件路径（由 parser 生成）
     * @return 成功返回 true，失败返回 false
     */
    bool BuildIndex(const std::string &raw_file_path) {
        std::ifstream in(raw_file_path, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            LOG(LogLevel::FATAL) << "open file: " + raw_file_path + " fail";
            return false;
        }
        // Index 是单例，测试和工具可能在同一进程里多次 InitSearcher。
        // 每次重新构建前必须清空旧正排、倒排和 BM25 统计，否则 IDF 与平均长度会混入旧语料。
        Reset();
        int cnt = 0;
        std::string line;
        while (std::getline(in, line)) {
            DocInfo *doc = BuildForwardIndex(line);
            if (!doc) {
                // 跳过格式异常的行，继续处理后续文档
                LOG(LogLevel::WARNING) << "BuildForwardIndex fail, skipping line";
                continue;
            }
            BuildInvertedIndex(*doc);
            cnt++;
            if (cnt % 5 == 0) {
                LOG(LogLevel::INFO) << "已建立索引: " + std::to_string(cnt);
            }
        }
        RefreshAverageFieldLength();
        return true;
    }

  private:
    void Reset() {
        // 单例索引允许测试或工具在同一进程内重新构建，重置统计可避免旧语料污染 BM25。
        forward_index.clear();
        inverted_index.clear();
        total_title_len = 0;
        total_content_len = 0;
        avg_title_len = 0.0;
        avg_content_len = 0.0;
    }

    void RefreshAverageFieldLength() {
        // BM25 的长度归一化依赖全局平均字段长度，空索引保持 0 让搜索阶段自然跳过。
        if (forward_index.empty()) {
            return;
        }
        // 平均长度在整个语料构建完成后一次性计算，保证每篇文档使用同一个全局基准。
        double doc_count = static_cast<double>(forward_index.size());
        avg_title_len = static_cast<double>(total_title_len) / doc_count;
        avg_content_len = static_cast<double>(total_content_len) / doc_count;
    }

    /**
     * @brief 从一行 raw 数据构建正排索引条目
     * @note 返回的指针在 forward_index 扩容后可能失效，调用方应即拿即用
     */
    DocInfo *BuildForwardIndex(std::string &line) {
        const std::string sep = "\3";
        std::vector<std::string> result;
        String_Util::Split(line, &result, sep);
        if (result.size() < 3) {
            LOG(LogLevel::LOG_ERROR) << "Split line failed, not enough fields";
            return nullptr;
        }

        DocInfo doc;
        doc.title = result[0];
        doc.content = result[1];
        doc.url = result[2];
        doc.doc_id = forward_index.size();

        forward_index.emplace_back(doc);
        return &forward_index.back();
    }

    /**
     * @brief 为单篇文档构建倒排索引
     *
     * 对标题和正文分别分词统计词频，并记录字段长度，供搜索阶段按 BM25 动态算分。
     */
    bool BuildInvertedIndex(DocInfo &doc) {
        struct WordCount {
            int title_count = 0;
            int content_count = 0;
        };
        // word_map存储这个词在当前文档中的 title和content 出现的数量
        std::unordered_map<std::string, WordCount> word_map;

        // 我们分别对title和content按CutForSearch来切词
        // 方便对当前文档的每一个词建立倒排索引
        std::vector<std::string> title_words;
        Jieba_util::CutString(doc.title, &title_words);
        // 字段长度与词频必须使用同一份分词结果，避免长度归一化和倒排词频口径不一致。
        doc.title_len = title_words.size();
        total_title_len += doc.title_len;
        for (auto word : title_words) {
            String_Util::ToLowerAscii(&word);
            word_map[word].title_count++;
        }

        std::vector<std::string> content_words;
        Jieba_util::CutString(doc.content, &content_words);
        // 正文通常远长于标题，单独统计正文长度可以让 BM25 抑制长文档的自然词频优势。
        doc.content_len = content_words.size();
        total_content_len += doc.content_len;
        for (auto word : content_words) {
            String_Util::ToLowerAscii(&word);
            word_map[word].content_count++;
        }

        for (auto &pair : word_map) {
            InvertedElem elem;
            elem.doc_id = doc.doc_id;
            elem.word = pair.first;
            // 倒排拉链中每个词只为当前文档写一条记录，字段词频都放在同一个 elem 里。
            // 这样搜索合并时能一次取到标题分和正文分，避免同一文档同一词重复累加。
            elem.title_count = pair.second.title_count;
            elem.content_count = pair.second.content_count;
            inverted_index[pair.first].emplace_back(elem);
        }
        return true;
    }
};

Index *Index::instance = nullptr;
std::mutex Index::mtx;

} // namespace ns_index
