#include "index.h"

#include "log.h"
#include "util.h"

#include <fstream>

namespace ns_index {

Index *Index::instance = nullptr;
std::mutex Index::mtx;

Index::Index() {}

Index::~Index() {}

Index *Index::GetInstance() {
    if (instance == nullptr) {
        std::lock_guard<std::mutex> lock(mtx);
        if (instance == nullptr) {
            instance = new Index;
        }
    }
    return instance;
}

DocInfo *Index::GetForwardIndex(uint64_t doc_id) {
    if (doc_id >= forward_index.size()) {
        LOG(LogLevel::WARNING) << "doc_id out of range";
        return nullptr;
    }
    return &forward_index[doc_id];
}

InvertedList *Index::FindInvertedList(const std::string &word) {
    auto ret = inverted_index.find(word);
    if (ret == inverted_index.end()) {
        return nullptr;
    }
    return &ret->second;
}

InvertedList *Index::GetInvertedList(std::string &word) {
    InvertedList *list = FindInvertedList(word);
    if (list == nullptr) {
        LOG(LogLevel::WARNING) << "can't find word: " + word + " in inverted_index";
        return nullptr;
    }
    return list;
}

const std::unordered_map<std::string, InvertedList> &Index::GetInvertedIndex() const { return inverted_index; }

size_t Index::GetDocCount() const { return forward_index.size(); }

double Index::GetAvgTitleLen() const { return avg_title_len; }

double Index::GetAvgContentLen() const { return avg_content_len; }

bool Index::BuildIndex(const std::string &raw_file_path) {
    std::ifstream in(raw_file_path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        LOG(LogLevel::FATAL) << "open file: " + raw_file_path + " fail";
        return false;
    }
    // Index 是单例，每次构建前必须清空旧语料统计，避免 BM25 全局值被污染。
    Reset();
    int cnt = 0;
    std::string line;
    while (std::getline(in, line)) {
        DocInfo *doc = BuildForwardIndex(line);
        if (!doc) {
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

void Index::Reset() {
    forward_index.clear();
    inverted_index.clear();
    total_title_len = 0;
    total_content_len = 0;
    avg_title_len = 0.0;
    avg_content_len = 0.0;
}

void Index::RefreshAverageFieldLength() {
    if (forward_index.empty()) {
        return;
    }
    double doc_count = static_cast<double>(forward_index.size());
    avg_title_len = static_cast<double>(total_title_len) / doc_count;
    avg_content_len = static_cast<double>(total_content_len) / doc_count;
}

DocInfo *Index::BuildForwardIndex(std::string &line) {
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

bool Index::BuildInvertedIndex(DocInfo &doc) {
    struct WordCount {
        int title_count = 0;
        int content_count = 0;
    };
    std::unordered_map<std::string, WordCount> word_map;

    std::vector<std::string> title_words;
    Jieba_util::CutString(doc.title, &title_words);
    // 字段长度和词频必须来自同一份分词结果，保证 BM25 归一化口径一致。
    doc.title_len = title_words.size();
    total_title_len += doc.title_len;
    for (auto word : title_words) {
        String_Util::ToLowerAscii(&word);
        word_map[word].title_count++;
    }

    std::vector<std::string> content_words;
    Jieba_util::CutString(doc.content, &content_words);
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
        elem.title_count = pair.second.title_count;
        elem.content_count = pair.second.content_count;
        inverted_index[pair.first].emplace_back(elem);
    }
    return true;
}

} // namespace ns_index
