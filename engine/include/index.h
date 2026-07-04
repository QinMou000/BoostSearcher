#pragma once

#include <cstddef>
#include <cstdint>
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
    size_t title_len = 0;
    size_t content_len = 0;
};

struct InvertedElem {
    uint64_t doc_id;
    std::string word;
    // 倒排项保留字段内原始词频，搜索阶段再结合 BM25 统计动态算分。
    int title_count = 0;
    int content_count = 0;
};

using InvertedList = std::vector<InvertedElem>;

class Index {
  private:
    std::vector<DocInfo> forward_index;
    std::unordered_map<std::string, InvertedList> inverted_index;
    // BM25 集合级统计只在构建阶段刷新，搜索阶段只读。
    size_t total_title_len = 0;
    size_t total_content_len = 0;
    double avg_title_len = 0.0;
    double avg_content_len = 0.0;

    Index();
    Index(const Index &) = delete;
    Index &operator=(const Index &) = delete;

    static Index *instance;
    static std::mutex mtx;

    void Reset();
    void RefreshAverageFieldLength();
    DocInfo *BuildForwardIndex(std::string &line);
    bool BuildInvertedIndex(DocInfo &doc);

  public:
    static Index *GetInstance();

    ~Index();

    DocInfo *GetForwardIndex(uint64_t doc_id);
    InvertedList *FindInvertedList(const std::string &word);
    InvertedList *GetInvertedList(std::string &word);
    const std::unordered_map<std::string, InvertedList> &GetInvertedIndex() const;

    size_t GetDocCount() const;
    double GetAvgTitleLen() const;
    double GetAvgContentLen() const;

    bool BuildIndex(const std::string &raw_file_path);
};

} // namespace ns_index
