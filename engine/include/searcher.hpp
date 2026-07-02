#pragma once
#include "index.hpp"
#include "log.hpp"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace ns_searcher {

class Searcher {
  private:
    ns_index::Index *index;

  public:
    Searcher() {}
    ~Searcher() {}

    void InitSearcher(const std::string &raw_file_path) {
        index = ns_index::Index::GetInstance();
        if (index == nullptr) {
            LOG(LogLevel::FATAL) << "获取单例失败";
            exit(1);
        }
        LOG(LogLevel::INFO) << "获取单例成功";
        index->BuildIndex(raw_file_path);
        LOG(LogLevel::INFO) << "建立索引成功";
    }

    /**
     * @brief 搜索并返回 JSON 结果
     *
     * 对查询词分词后查倒排索引，合并同文档结果并按权重排序，
     * 返回 top-N 结果（包含标题、摘要、URL）的 JSON 数组。
     */
    void Search(const std::string &query, std::string *json) {
        std::vector<std::string> words;
        Jieba_util::CutString(query, &words);

        struct MergedResult {
            uint64_t doc_id = 0;
            int sum_weight = 0;
            std::vector<std::string> matched_words;
        };

        std::unordered_map<uint64_t, MergedResult> result_map;

        for (std::string word : words) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            ns_index::InvertedList *inverted_list =
                index->GetInvertedList(word);
            if (inverted_list == nullptr) {
                continue;
            }
            for (auto &item : *inverted_list) {
                // 将同一文档的结果合并，累加权重并记录匹配的关键词
                auto &merged = result_map[item.doc_id];
                merged.doc_id = item.doc_id;
                merged.sum_weight += item.weight;
                merged.matched_words.emplace_back(item.word);
            }
        }

        if (result_map.empty()) {
            LOG(LogLevel::WARNING) << "No relevant results";
            return;
        }

        std::vector<MergedResult> results;
        results.reserve(result_map.size());
        for (auto &item : result_map) {
            results.emplace_back(std::move(item.second));
        }

        // 先排序再截取 top-N
        std::sort(results.begin(), results.end(),
                  [](const MergedResult &a, const MergedResult &b) {
                      return a.sum_weight > b.sum_weight;
                  });

        constexpr size_t kMaxResults = 100;
        if (results.size() > kMaxResults) {
            results.resize(kMaxResults);
        }

        nlohmann::json root = nlohmann::json::array();
        for (auto &item : results) {
            ns_index::DocInfo *doc = index->GetForwardIndex(item.doc_id);
            if (doc == nullptr) {
                continue;
            }
            nlohmann::json elem;
            elem["title"] = doc->title;
            if (!item.matched_words.empty()) {
                elem["desc"] = GetAbstract(doc->content, item.matched_words[0]);
            }
            elem["url"] = doc->url;
            root.push_back(elem);
        }

        *json =
            root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

  private:
    // 根据关键词在文档中的位置，截取前后一定范围作为摘要
    std::string GetAbstract(const std::string &content,
                            const std::string &word) {
        auto it = std::search(
            content.begin(), content.end(), word.begin(), word.end(),
            [](char x, char y) { return std::tolower(x) == std::tolower(y); });
        if (it == content.end()) {
            return "None";
        }

        long long pos = std::distance(content.begin(), it);
        long long start = 0;
        long long end = content.size();

        if (pos > 50) {
            start = pos - 50;
        }
        if (pos + 100 < end) {
            end = pos + 100;
        }
        if (start >= end) {
            return "";
        }
        return content.substr(start, end - start);
    }
};

} // namespace ns_searcher
