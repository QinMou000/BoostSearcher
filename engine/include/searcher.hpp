#pragma once
#include "index.hpp"
#include "log.hpp"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>

namespace ns_searcher {

class Searcher {
  private:
    // 倒排拉链指针 + 本次召回权重百分比。精确命中使用 100，
    // 模糊命中使用较低权重，避免错字结果压过真正精确匹配的结果。
    typedef std::pair<const ns_index::InvertedList *, int> WeightedInvertedList;

    /**
     * @brief 为一个未精确命中的词查找模糊候选倒排拉链
     *
     * 第一版实现优先保证行为简单可控：只有精确查不到时才进入这里，
     * 然后遍历倒排词典，用编辑距离找相似词。这样不会影响原有精确搜索的
     * 排序稳定性，也能先覆盖用户输入错别字、少字、多字的场景。
     */
    std::vector<WeightedInvertedList> GetFuzzyInvertedLists(const std::string &word) {
        // 限制每个 query 词最多扩展 5 个相似词，避免一个错词把结果集
        // 冲得太散。
        constexpr size_t kMaxFuzzyTerms = 5;
        // 模糊命中是“补召回”，相关性弱于精确命中，所以只给 60% 权重。
        constexpr int kFuzzyWeightPercent = 60;

        std::vector<std::string> chars = SplitUtf8Chars(word);
        size_t max_distance = GetMaxEditDistance(chars.size());
        if (max_distance == 0) {
            return {};
        }

        struct Candidate {
            const ns_index::InvertedList *list = nullptr;
            size_t distance = 0;
            size_t term_size = 0;
        };

        std::vector<Candidate> candidates;
        for (const auto &pair : index->GetInvertedIndex()) {
            std::vector<std::string> term_chars = SplitUtf8Chars(pair.first);
            size_t term_size = term_chars.size();
            // 计算长度差，提前剪枝
            size_t size_diff = chars.size() > term_size ? chars.size() - term_size : term_size - chars.size();
            // 长度差已经超过允许编辑距离时，真实编辑距离必然更大，直接跳过。
            if (size_diff > max_distance) {
                continue;
            }

            size_t distance = EditDistanceWithinLimit(chars, term_chars, max_distance);
            if (distance <= max_distance) {
                candidates.push_back({&pair.second, distance, term_size});
            }
        }

        // 距离越小越像；距离相同则优先短词，减少长词“碰巧包含
        // 相似片段”带来的噪声。
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            if (a.distance != b.distance) {
                return a.distance < b.distance;
            }
            return a.term_size < b.term_size;
        });

        std::vector<WeightedInvertedList> result;
        size_t result_size = std::min(kMaxFuzzyTerms, candidates.size());
        result.reserve(result_size);
        for (size_t i = 0; i < result_size; ++i) {
            result.emplace_back(candidates[i].list, kFuzzyWeightPercent);
        }
        return result;
    }

    /**
     * @brief 根据词长决定允许的最大编辑距离
     *
     * 短词容错太宽会产生大量误召回，所以 1 个字符不做模糊；
     * 2~4 个字符只允许 1 次编辑；更长的词允许 2 次编辑。
     */
    size_t GetMaxEditDistance(size_t char_count) {
        if (char_count <= 1) {
            return 0;
        }
        if (char_count <= 4) {
            return 1;
        }
        return 2;
    }

    /**
     * @brief 按 UTF-8 编码切分“字符”
     *
     * 中文在 std::string 里是多字节，如果直接按 byte 计算编辑距离，
     * 一个汉字会被当成 3 个字符，距离会被严重放大。这里把每个 UTF-8
     * code point 切成一个 string，后续编辑距离按“字”而不是按 byte 计算。
     */
    std::vector<std::string> SplitUtf8Chars(const std::string &text) {
        std::vector<std::string> chars;
        // i 不自动自增，由 char_len 控制跳跃
        for (size_t i = 0; i < text.size();) {
            // 获取当前位置UTF8字符字节长度
            size_t char_len = GetUtf8CharLength(text[i]);
            // 遇到非法或截断的 UTF-8 字节时退化为单字节，保证搜索流程
            // 不中断。
            if (char_len == 0 || i + char_len > text.size()) {
                char_len = 1;
            }
            chars.emplace_back(text.substr(i, char_len));
            i += char_len;
        }
        return chars;
    }

    /**
     * @brief 判断当前 UTF-8 字符占用的字节数
     *
     * 输入 UTF-8 字符串的第一个字节，返回这个完整 UTF-8
     * 字符一共占几个字节；非法首字节返回 0。
     */
    size_t GetUtf8CharLength(char c) {
        unsigned char byte = static_cast<unsigned char>(c);
        if ((byte & 0x80) == 0) {
            return 1;
        }
        if ((byte & 0xE0) == 0xC0) {
            return 2;
        }
        if ((byte & 0xF0) == 0xE0) {
            return 3;
        }
        if ((byte & 0xF8) == 0xF0) {
            return 4;
        }
        return 0;
    }

    /**
     * @brief 计算编辑距离，但超过 limit 后尽早停止
     *
     * 模糊搜索只关心“是否在阈值内”，不需要完整精确距离。每一行 DP
     * 如果最小值已经超过 limit，后续只会更大，可以直接返回 limit + 1。
     * 空间上只保留上一行和当前行，避免为每个候选词分配完整二维表。
     */
    size_t EditDistanceWithinLimit(const std::vector<std::string> &left, const std::vector<std::string> &right,
                                   size_t limit) {
        std::vector<size_t> prev(right.size() + 1);
        std::vector<size_t> curr(right.size() + 1);

        for (size_t j = 0; j <= right.size(); ++j) {
            prev[j] = j; // 空字符串匹配长度 j 的串，只能全部插入，距离 = j。
        }
        /**
         * dp[i][j] 表示 left 前 i 个字符转换到 right 前 j 个字符的编辑距离
         *      right
         *        0 1 2 3 4
         * left     a b c d
         *  0     0 1 2 3 4    prev
         *  1 a   1            curr
         *  2 b   2
         *  3 v   3
         *  4 d   4
         */
        for (size_t i = 1; i <= left.size(); ++i) {
            curr[0] = i; // right 为空，left 前 i 个字符只能全部删除
            size_t row_min = curr[0];
            for (size_t j = 1; j <= right.size(); ++j) {
                size_t cost = left[i - 1] == right[j - 1] ? 0 : 1;
                curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
                row_min = std::min(row_min, curr[j]);
            }
            if (row_min > limit) {
                // 剪枝 当前行最小值已经超过 limit，后续只会更大，直接返回limit + 1
                return limit + 1;
            }
            prev.swap(curr); // 交换上一行和当前行，进入下一行计算
        }

        return prev[right.size()];
    }
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

        // 同一个文档可能被多个词命中；统一从这里合并权重和命中词。
        // weight_percent 用来区分精确命中和模糊命中，保持排序逻辑集中。
        auto merge_inverted_list = [&](const ns_index::InvertedList *list, int weight_percent) {
            for (auto &item : *list) {
                int weight = item.weight * weight_percent / 100;
                // 对低权重词做百分比折算时，至少保留 1 分，避免召回后
                // 被算成 0。
                if (weight <= 0) {
                    weight = 1;
                }
                auto &merged = result_map[item.doc_id];
                merged.doc_id = item.doc_id;
                merged.sum_weight += weight;
                merged.matched_words.emplace_back(item.word);
            }
        };

        for (std::string word : words) {
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);

            // 精确命中优先：只要原词能查到，就不再扩展模糊词，避免
            // 引入噪声。
            ns_index::InvertedList *inverted_list = index->FindInvertedList(word);
            if (inverted_list != nullptr) {
                merge_inverted_list(inverted_list, 100);
                continue;
            }

            // 精确未命中才进行模糊召回，例如“协义”可以补到“协议”。
            for (auto &fuzzy_item : GetFuzzyInvertedLists(word)) {
                // fuzzy_item 内部已经带了降权比例，合并逻辑仍然复用同一处。
                merge_inverted_list(fuzzy_item.first, fuzzy_item.second);
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
                  [](const MergedResult &a, const MergedResult &b) { return a.sum_weight > b.sum_weight; });

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

        *json = root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

  private:
    // 根据关键词在文档中的位置，截取前后一定范围作为摘要
    std::string GetAbstract(const std::string &content, const std::string &word) {
        auto it = std::search(content.begin(), content.end(), word.begin(), word.end(),
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
