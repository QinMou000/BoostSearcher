#pragma once
#include "index.hpp"
#include <algorithm>

namespace ns_searcher
{
    class Searcher
    {
    private:
        ns_index::Index *index;

    public:
        Searcher() {}
        ~Searcher() {}
        void InitSearcher(const std::string &raw_file_path)
        {
            // 获取index单例
            index = ns_index::Index::GetInstance();
            // 建立索引
            index->BuildIndex(raw_file_path);
        }
        // 用户给我一个关键字 我返回一个 json 串
        void Search(const std::string &query, std::string *json)
        {
            // 对关键字进行切词
            std::vector<std::string> words;
            Jieba_util::CutString(query, &words);

            // 根据切词结果 到索引里面去找
            ns_index::InvertedList inverted_list_all;

            for (std::string word : words)
            {
                boost::to_lower(word);                                                                           // 转小写
                ns_index::InvertedList *inverted_list = index->GetInvertedList(word);                            // 拿到倒排拉链
                inverted_list_all.insert(inverted_list_all.end(), inverted_list->begin(), inverted_list->end()); // 将找到的倒排拉链里面的元素都插进新的拉链里面
            }

            // 根据找到的结果 按照权重降序排序
            std::sort(inverted_list_all.begin(), inverted_list_all.end(),
                      [](const ns_index::InvertedElem &e1, const ns_index::InvertedElem &e2)
                      { return e1.weight > e2.weight; });
            // 构建 json 串并返回

        }
    };
}
