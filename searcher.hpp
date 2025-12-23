#pragma once
#include "index.hpp"
#include "log.hpp"
#include <algorithm>
#include <json/json.h>

namespace ns_searcher {
    class Searcher {
      private:
        ns_index::Index *index;

      public:
        Searcher() {}
        ~Searcher() {}
        void InitSearcher(const std::string &raw_file_path) {
            // 获取index单例
            index = ns_index::Index::GetInstance();
            if (index == nullptr) {
                // std::cerr << "获取单例失败" << std::endl;
                LOG(FATAL, "获取单例失败");
                exit(1);
            }
            // std::cout << "获取单例成功" << std::endl;
            LOG(INFO, "获取单例成功");
            // 建立索引
            index->BuildIndex(raw_file_path);
            // std::cout << "建立索引成功" << std::endl;
            LOG(INFO, "建立索引成功");
        }
        // 用户给我一个关键字 我返回一个 json 串
        /**
         * @brief Performs a search for the given query string and returns results in JSON format.
         *
         * This function tokenizes the input query using Jieba, retrieves relevant documents from the inverted index,
         * merges results for documents matching multiple query terms, sorts the results by relevance (weight),
         * and constructs a JSON array of up to 100 top results. Each result includes the document's title, a generated
         * abstract based on the most relevant word, and the document's URL.
         *
         * @param query The search query string.
         * @param json Pointer to a string where the resulting JSON will be stored.
         */
        void Search(const std::string &query, std::string *json) {
            // 对关键字进行切词
            std::vector<std::string> words;
            Jieba_util::CutString(query, &words);

            struct invertedElem_to_merge {
                uint64_t doc_id;                // 文档id
                int sum_weight;                 // 这个文档在本次搜索中占的总权值
                std::vector<std::string> Words; // 本次搜索被切分的词中和文档内容相关的词
            };

            // 根据切词结果 到索引里面去找
            // ns_index::InvertedList inverted_list_all;

            std::unordered_map<uint64_t, invertedElem_to_merge> map; // 避免多个词搜索到同一个文档的情况

            for (std::string word : words) {
                boost::to_lower(word);                                                // 转小写
                ns_index::InvertedList *inverted_list = index->GetInvertedList(word); // 拿到倒排拉链
                if (inverted_list == nullptr) {
                    continue;
                }
                for (auto &item : *inverted_list) {
                    // 将拿到的倒排拉链中的元素插入到hash里面去重
                    auto &Second = map[item.doc_id]; // 需要加引用 不加引用并不会改变map里面的值
                    Second.doc_id = item.doc_id;
                    Second.sum_weight += item.weight;     // 将所有文档的权值累加
                    Second.Words.emplace_back(item.word); // 依旧减少拷贝
                }

                // inverted_list_all.insert(inverted_list_all.end(), inverted_list->begin(), inverted_list->end()); // 将找到的倒排拉链里面的元素都插进新的拉链里面
            }
            std::vector<invertedElem_to_merge> inverted_list_all;
            for (auto &item : map) {
                inverted_list_all.emplace_back(item.second);
            }
            // 保留100个结果
            inverted_list_all.resize(100);

            // 根据找到的结果 按照权重降序排序
            std::sort(inverted_list_all.begin(), inverted_list_all.end(),
            [](const invertedElem_to_merge & e1, const invertedElem_to_merge & e2) {
                return e1.sum_weight > e2.sum_weight;
            });
            // 构建 json 串并返回
            Json::Value root;
            for (auto &item : inverted_list_all) { // 已经是按降序排列好的
                ns_index::DocInfo *doc = index->GetForwardIndex(item.doc_id); // 根据 doc_id 和正排索引 找到文档
                if (doc == nullptr) {
                    continue;
                }
                Json::Value it; // 为单个文档构建 json
                it["title"] = doc->title;
                if (item.Words.empty()) {
                    // std::cerr << "No relavent words" << std::endl;
                    LOG(WARNING, "No relavent words");
                } else
                    it["desc"] = GetAbstract(doc->content /*这里不能拿网页原始内容需要转小写 */,
                                             item.Words[0] /*数组第一个词传进去找*/); // TODO 不能把全部内容都加进去 // 需要获取摘要
                it["url"] = doc->url;

                // FOR DEBUG
                // it["doc_id"] = (int)doc->doc_id;
                // it["weight"] = item.weight;

                root.append(it); // 将当前遍历到的文档插入
            }

            Json::StyledWriter Write;
            *json = Write.write(root); // 将搜索到的全部文档内容写到 Write 里面
        }

        std::string GetAbstract(const std::string &content, const std::string &word) {
            // 在当前内容中找到 word 位置
            // long long pos = content.find(word);
            // if (pos == std::string::npos)
            //     return "None";

            auto it = std::search(content.begin(), content.end(), word.begin(), word.end(), [](char x, char y) {
                return std::tolower(x) == std::tolower(y);
            });
            if (it == content.end()) {
                return "None";

            }

            long long pos = std::distance(content.begin(), it);

            // 确定摘要范围
            long long start = 0;
            long long end = content.size() - 1; // 严谨一点~

            if (pos - 50 > 0) // 控制长度
                start = pos - { 50;
            if (pos + 100 < end)
            }
                end = pos + 100; {


            }
            // std::cout << pos << " " << start << " " << end << std::endl;
            if (start > end)
                return "start > end ? ";
 {
            // 切分字符串
            }
            return content.substr(start, end - start);
        }
    };
}
