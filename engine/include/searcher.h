#pragma once

#include "index.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ns_searcher {

class Searcher {
  private:
    // 倒排拉链指针 + 本次召回权重百分比，用于统一精确召回和模糊召回的合并流程。
    using WeightedInvertedList = std::pair<const ns_index::InvertedList *, int>;

    std::vector<WeightedInvertedList> GetFuzzyInvertedLists(const std::string &word);
    size_t GetMaxEditDistance(size_t char_count);
    std::vector<std::string> SplitUtf8Chars(const std::string &text);
    size_t GetUtf8CharLength(char c);
    size_t EditDistanceWithinLimit(const std::vector<std::string> &left, const std::vector<std::string> &right,
                                   size_t limit);

    static bool IsAsciiSpace(char ch);
    static std::string NormalizePhrase(std::string text, bool remove_spaces);
    static double GetPhraseBoost(const ns_index::DocInfo &doc, const std::string &phrase,
                                 const std::string &compact_phrase);
    double GetInverseDocumentFrequency(size_t doc_freq) const;

    static double GetBm25FieldScore(int term_freq, size_t field_len, double avg_field_len, double idf);

    double GetBm25Score(const ns_index::InvertedElem &item, const ns_index::DocInfo &doc, double idf) const;
    static bool IsStopWord(const std::string &word);
    static void FilterSearchWords(std::vector<std::string> *words);
    std::string GetAbstract(const std::string &content, const std::string &word);

    ns_index::Index *index = nullptr;

  public:
    Searcher();
    ~Searcher();

    void InitSearcher(const std::string &raw_file_path);

    void Search(const std::string &query, std::string *json);
};

} // namespace ns_searcher
