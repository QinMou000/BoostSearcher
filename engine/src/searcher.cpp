#include "searcher.h"

#include "log.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace ns_searcher {

std::vector<Searcher::WeightedInvertedList> Searcher::GetFuzzyInvertedLists(const std::string &word) {
    constexpr size_t kMaxFuzzyTerms = 5;
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
    // 当前模糊召回仍是 O(N) 遍历倒排词典；后续可替换为 BK-tree 或 ngram 候选索引。
    // 只有精确倒排未命中时才会进入这里，因此先保证行为简单可控，避免模糊候选污染精确结果。
    for (const auto &pair : index->GetInvertedIndex()) {
        std::vector<std::string> term_chars = SplitUtf8Chars(pair.first);
        size_t term_size = term_chars.size();
        // 长度差超过最大编辑距离时，真实编辑距离必然更大，可以提前剪枝减少 DP 次数。
        size_t size_diff = chars.size() > term_size ? chars.size() - term_size : term_size - chars.size();
        if (size_diff > max_distance) {
            continue;
        }

        size_t distance = EditDistanceWithinLimit(chars, term_chars, max_distance);
        if (distance <= max_distance) {
            candidates.push_back({&pair.second, distance, term_size});
        }
    }

    // 候选词越接近用户输入越靠前；距离相同优先短词，减少长词包含短片段带来的噪声。
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

size_t Searcher::GetMaxEditDistance(size_t char_count) {
    // 单字容错非常容易误召回，直接禁用；短词只允许 1 次编辑，长词允许 2 次编辑。
    if (char_count <= 1) {
        return 0;
    }
    if (char_count <= 4) {
        return 1;
    }
    return 2;
}

std::vector<std::string> Searcher::SplitUtf8Chars(const std::string &text) {
    std::vector<std::string> chars;
    for (size_t i = 0; i < text.size();) {
        size_t char_len = GetUtf8CharLength(text[i]);
        // 非法或截断 UTF-8 不让搜索流程中断，退化成单字节处理即可。
        if (char_len == 0 || i + char_len > text.size()) {
            char_len = 1;
        }
        chars.emplace_back(text.substr(i, char_len));
        i += char_len;
    }
    return chars;
}

size_t Searcher::GetUtf8CharLength(char c) {
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

size_t Searcher::EditDistanceWithinLimit(const std::vector<std::string> &left, const std::vector<std::string> &right,
                                         size_t limit) {
    // 只保存上一行和当前行，空间复杂度从 O(mn) 降到 O(n)。
    std::vector<size_t> prev(right.size() + 1);
    std::vector<size_t> curr(right.size() + 1);

    for (size_t j = 0; j <= right.size(); ++j) {
        prev[j] = j;
    }
    for (size_t i = 1; i <= left.size(); ++i) {
        curr[0] = i;
        size_t row_min = curr[0];
        for (size_t j = 1; j <= right.size(); ++j) {
            size_t cost = left[i - 1] == right[j - 1] ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
            row_min = std::min(row_min, curr[j]);
        }
        if (row_min > limit) {
            // 当前行最小值已经超过阈值，后续行不会重新回到阈值内，直接返回“超限”哨兵。
            return limit + 1;
        }
        prev.swap(curr);
    }

    return prev[right.size()];
}

bool Searcher::IsAsciiSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

std::string Searcher::NormalizePhrase(std::string text, bool remove_spaces) {
    String_Util::ToLowerAscii(&text);
    if (remove_spaces) {
        // 去空白只用于短语匹配兼容，例如用户输入 “TCP IP” 时也能命中 “TCPIP”。
        text.erase(std::remove_if(text.begin(), text.end(), IsAsciiSpace), text.end());
    }
    return text;
}

double Searcher::GetPhraseBoost(const ns_index::DocInfo &doc, const std::string &phrase,
                                const std::string &compact_phrase) {
    // 短语加成只做同分附近的相关性校正，不能回到上万级常量压过 BM25 主分。
    constexpr double kTitlePhraseBoost = 8.0;
    constexpr double kContentPhraseBoost = 4.0;
    const bool need_compact_match = !compact_phrase.empty() && compact_phrase != phrase;

    std::string title = NormalizePhrase(doc.title, false);
    if (title.find(phrase) != std::string::npos) {
        return kTitlePhraseBoost;
    }
    if (need_compact_match && NormalizePhrase(doc.title, true).find(compact_phrase) != std::string::npos) {
        return kTitlePhraseBoost;
    }

    std::string content = NormalizePhrase(doc.content, false);
    if (content.find(phrase) != std::string::npos) {
        return kContentPhraseBoost;
    }
    if (need_compact_match && NormalizePhrase(doc.content, true).find(compact_phrase) != std::string::npos) {
        return kContentPhraseBoost;
    }
    return 0.0;
}

double Searcher::GetInverseDocumentFrequency(size_t doc_freq) const {
    size_t doc_count = index->GetDocCount();
    if (doc_count == 0 || doc_freq == 0) {
        return 0.0;
    }
    double total_docs = static_cast<double>(doc_count);
    double matched_docs = static_cast<double>(doc_freq);
    // 使用 log(1 + ...) 的 BM25 变体，避免高频词在小语料或极端语料下产生负分。
    return std::log(1.0 + (total_docs - matched_docs + 0.5) / (matched_docs + 0.5));
}

/**
 * @brief 计算某一个字段里的 BM25 子分数
 *
 * @param term_freq 当前查询词在这个字段中出现的次数，来自 InvertedElem::title_count 或 content_count。
 *                  值越大说明字段内匹配越强，但 BM25 会让词频收益逐渐饱和，避免堆词刷分。
 * @param field_len 当前文档这个字段的分词长度，来自 DocInfo::title_len 或 content_len。
 *                  这里的长度是 token 数，不是字节数；字段越长，同样词频越需要被归一化惩罚。
 * @param avg_field_len 整个语料中同类字段的平均分词长度，来自 Index::GetAvgTitleLen 或 GetAvgContentLen。
 *                      它是长度归一化的全局基准；为 0 时说明索引为空或统计不可用，直接返回 0。
 * @param idf 当前查询词的逆文档频率，由 GetInverseDocumentFrequency 根据倒排拉链长度计算。
 *            它表示词本身的区分度；越稀有的词 idf 越高，对排序影响越大。
 */
double Searcher::GetBm25FieldScore(int term_freq, size_t field_len, double avg_field_len, double idf) {
    // k1 控制词频收益饱和速度，b 控制字段长度归一化强度；先采用常见默认值作为基线。
    constexpr double kBm25K1 = 1.5;
    constexpr double kBm25B = 0.75;
    if (term_freq <= 0 || avg_field_len <= 0.0 || idf <= 0.0) {
        return 0.0;
    }

    double tf = static_cast<double>(term_freq);
    // 字段越长，length_ratio 越大，同样词频会被更强地归一化，避免长正文堆词刷分。
    double length_ratio = static_cast<double>(field_len) / avg_field_len;
    double normalized_length = 1.0 - kBm25B + kBm25B * length_ratio;
    double denominator = tf + kBm25K1 * normalized_length;
    if (denominator <= 0.0) {
        return 0.0;
    }
    return idf * (tf * (kBm25K1 + 1.0)) / denominator;
}

double Searcher::GetBm25Score(const ns_index::InvertedElem &item, const ns_index::DocInfo &doc, double idf) const {
    constexpr double kTitleFieldWeight = 3.0;
    constexpr double kContentFieldWeight = 1.0;
    // 第一版采用轻量 BM25F：标题和正文各自 BM25，再用字段权重合并。
    double title_score =
        GetBm25FieldScore(item.title_count, doc.title_len, index->GetAvgTitleLen(), idf) * kTitleFieldWeight;
    double content_score =
        GetBm25FieldScore(item.content_count, doc.content_len, index->GetAvgContentLen(), idf) * kContentFieldWeight;
    return title_score + content_score;
}

bool Searcher::IsStopWord(const std::string &word) {
    static const std::unordered_set<std::string> stop_words = {
        "的",   "了",   "和",   "在",   "我",   "是", "对", "把", "被", "与", "以及", "一个",
        "这个", "那个", "什么", "怎么", "如何", "吗", "呢", "啊", "吧", "中", "上",   "下"};
    return stop_words.find(word) != stop_words.end();
}

void Searcher::FilterSearchWords(std::vector<std::string> *words) {
    words->erase(std::remove_if(words->begin(), words->end(),
                                [](std::string &word) {
                                    String_Util::ToLowerAscii(&word);
                                    return IsStopWord(word);
                                }),
                 words->end());
}

Searcher::Searcher() {}

Searcher::~Searcher() {}

void Searcher::InitSearcher(const std::string &raw_file_path) {
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
 * 对外接口保持不变：调用方传入 query，函数把 JSON 数组写入 json。
 */
void Searcher::Search(const std::string &query, std::string *json) {
    std::vector<std::string> words;
    Jieba_util::CutString(query, &words);
    // 停用词只表达语法关系，不参与倒排召回，避免污染排序和前端高亮词。
    FilterSearchWords(&words);
    if (words.empty()) {
        *json = "[]";
        return;
    }

    struct MergedResult {
        uint64_t doc_id = 0;
        double score = 0.0;
        std::vector<std::string> matched_words;
    };

    std::unordered_map<uint64_t, MergedResult> result_map;

    auto merge_inverted_list = [&](const ns_index::InvertedList *list, int weight_percent) {
        // 同一条倒排拉链共享同一个 IDF，只需计算一次；weight_percent 区分精确与模糊召回。
        double idf = GetInverseDocumentFrequency(list->size());
        if (idf <= 0.0) {
            return;
        }
        for (const auto &item : *list) {
            ns_index::DocInfo *doc = index->GetForwardIndex(item.doc_id);
            if (doc == nullptr) {
                continue;
            }
            double score = GetBm25Score(item, *doc, idf) * static_cast<double>(weight_percent) / 100.0;
            if (score <= 0.0) {
                continue;
            }
            auto &merged = result_map[item.doc_id];
            merged.doc_id = item.doc_id;
            // 多个 query 词命中同一文档时累加相关性分数，体现多词共同匹配优势。
            merged.score += score;
            merged.matched_words.emplace_back(item.word);
        }
    };

    for (std::string word : words) {
        String_Util::ToLowerAscii(&word);

        // 精确命中优先；只有原词没有倒排拉链时才扩展模糊词，减少噪声召回。
        ns_index::InvertedList *inverted_list = index->FindInvertedList(word);
        if (inverted_list != nullptr) {
            merge_inverted_list(inverted_list, 100);
            continue;
        }

        for (auto &fuzzy_item : GetFuzzyInvertedLists(word)) {
            merge_inverted_list(fuzzy_item.first, fuzzy_item.second);
        }
    }

    if (result_map.empty()) {
        LOG(LogLevel::WARNING) << "No relevant results";
        return;
    }

    std::string phrase = NormalizePhrase(query, false);
    std::string compact_phrase = NormalizePhrase(query, true);
    std::vector<MergedResult> results;
    results.reserve(result_map.size());
    for (auto &item : result_map) {
        ns_index::DocInfo *doc = index->GetForwardIndex(item.first);
        if (doc != nullptr) {
            // 短语加成在候选合并完成后统一补充，避免额外遍历正排索引。
            item.second.score += GetPhraseBoost(*doc, phrase, compact_phrase);
        }
        results.emplace_back(std::move(item.second));
    }

    std::sort(results.begin(), results.end(), [](const MergedResult &a, const MergedResult &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        // 浮点同分时按 doc_id 稳定排序，避免 unordered_map 遍历顺序影响结果抖动。
        return a.doc_id < b.doc_id;
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
        elem["keywords"] = item.matched_words;
        root.push_back(elem);
    }

    *json = root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string Searcher::GetAbstract(const std::string &content, const std::string &word) {
    std::vector<std::string> content_chars = SplitUtf8Chars(content);
    std::vector<std::string> word_chars = SplitUtf8Chars(word);
    if (word_chars.empty() || content_chars.size() < word_chars.size()) {
        return "None";
    }

    size_t pos = std::string::npos;
    size_t max_start = content_chars.size() - word_chars.size();
    for (size_t start = 0; start <= max_start; ++start) {
        bool matched = true;
        for (size_t offset = 0; offset < word_chars.size(); ++offset) {
            // 摘要定位按 UTF-8 字符片段比较，避免按 byte 截断中文。
            std::string content_char = content_chars[start + offset];
            std::string word_char = word_chars[offset];
            String_Util::ToLowerAscii(&content_char);
            String_Util::ToLowerAscii(&word_char);
            if (content_char != word_char) {
                matched = false;
                break;
            }
        }
        if (matched) {
            pos = start;
            break;
        }
    }
    if (pos == std::string::npos) {
        return "None";
    }

    constexpr size_t kBeforeChars = 30;
    constexpr size_t kAfterChars = 60;
    size_t start = pos > kBeforeChars ? pos - kBeforeChars : 0;
    size_t end = std::min(content_chars.size(), pos + word_chars.size() + kAfterChars);
    if (start >= end) {
        return "";
    }

    std::string result;
    for (size_t i = start; i < end; ++i) {
        // 按字符片段拼接摘要，保证边界不会落在中文多字节字符中间。
        result += content_chars[i];
    }
    return result;
}

} // namespace ns_searcher
