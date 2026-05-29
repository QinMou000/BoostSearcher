#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <cctype>
#include "util.hpp"
#include "log.hpp"

namespace ns_index {

struct DocInfo {
    std::string title;
    std::string content;
    std::string url;
    uint64_t doc_id;
};

struct InvertedElem {
    uint64_t doc_id;
    std::string word;
    int weight;
};

typedef std::vector<InvertedElem> InvertedList;

class Index {
  private:
    std::vector<DocInfo> forward_index;                            // 正排索引，用数组下标作为文档 id
    std::unordered_map<std::string, InvertedList> inverted_index;  // 倒排索引，关键字到倒排拉链的映射

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

    InvertedList *GetInvertedList(std::string &word) {
        auto ret = inverted_index.find(word);
        if (ret == inverted_index.end()) {
            LOG(LogLevel::WARNING) << "can't find word: " + word + " in inverted_index";
            return nullptr;
        }
        return &ret->second;
    }

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
            if (cnt % 50 == 0) {
                LOG(LogLevel::INFO) << "已建立索引: " + std::to_string(cnt);
            }
        }
        return true;
    }

  private:
    /**
     * @brief 从一行 raw 数据构建正排索引条目
     * @note 返回的指针在 forward_index 扩容后可能失效，调用方应即拿即用
     */
    DocInfo *BuildForwardIndex(std::string &line) {
        const std::string sep = "\3";
        std::vector<std::string> result;
        String_Util::Split(line, &result, sep);
        if (result.size() < 3) {
            LOG(LogLevel::ERROR) << "Split line failed, not enough fields";
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
     * 对标题和正文分别分词统计词频，按加权公式计算权重后写入倒排索引。
     * 标题中出现的词权重更高（标题权重 10，正文权重 1）。
     */
    bool BuildInvertedIndex(const DocInfo &doc) {
        constexpr int kTitleWeight = 10;
        constexpr int kContentWeight = 1;

        struct WordCount {
            int title_count = 0;
            int content_count = 0;
        };

        std::unordered_map<std::string, WordCount> word_map;

        std::vector<std::string> title_words;
        Jieba_util::CutString(doc.title, &title_words);
        for (auto word : title_words) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            word_map[word].title_count++;
        }

        std::vector<std::string> content_words;
        Jieba_util::CutString(doc.content, &content_words);
        for (auto word : content_words) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            word_map[word].content_count++;
        }

        for (auto &pair : word_map) {
            InvertedElem elem;
            elem.doc_id = doc.doc_id;
            elem.word = pair.first;
            elem.weight = pair.second.title_count * kTitleWeight +
                          pair.second.content_count * kContentWeight;
            inverted_index[pair.first].emplace_back(elem);
        }
        return true;
    }
};

Index *Index::instance = nullptr;
std::mutex Index::mtx;

} // namespace ns_index
