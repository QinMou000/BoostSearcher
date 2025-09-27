#pragma once
#include "index.hpp"
#include <algorithm>
#include <json/json.h>

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
            std::cout << "获取单例成功" << std::endl;
            // 建立索引
            index->BuildIndex(raw_file_path);
            std::cout << "建立索引成功" << std::endl;
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
                boost::to_lower(word);                                                // 转小写
                ns_index::InvertedList *inverted_list = index->GetInvertedList(word); // 拿到倒排拉链
                if (inverted_list == nullptr)
                    continue;
                inverted_list_all.insert(inverted_list_all.end(), inverted_list->begin(), inverted_list->end()); // 将找到的倒排拉链里面的元素都插进新的拉链里面
            }
            // 保留100个结果
            inverted_list_all.resize(50);

            // 根据找到的结果 按照权重降序排序
            std::sort(inverted_list_all.begin(), inverted_list_all.end(),
                      [](const ns_index::InvertedElem &e1, const ns_index::InvertedElem &e2)
                      { return e1.weight > e2.weight; });
            // 构建 json 串并返回
            Json::Value root;
            for (auto &item : inverted_list_all) // 已经是按降序排列好的
            {
                ns_index::DocInfo *doc = index->GetForwardIndex(item.doc_id); // 根据 doc_id 和正排索引 找到文档
                if (doc == nullptr)
                    continue;
                Json::Value it; // 为单个文档构建 json
                it["title"] = doc->title;
                it["desc"] = GetAbstract(doc->content /*这里不能拿网页原始内容需要转小写 */, item.word); // TODO 不能把全部内容都加进去 // 需要获取摘要
                it["url"] = doc->url;

                // FOR DEBUG
                // it["doc_id"] = (int)doc->doc_id;
                // it["weight"] = item.weight;

                root.append(it); // 将当前遍历到的文档插入
            }

            Json::StyledWriter Write;
            *json = Write.write(root); // 将搜索到的全部文档内容写到 Write 里面
        }

        std::string GetAbstract(const std::string &content, const std::string &word)
        {
            // 在当前内容中找到 word 位置
            // long long pos = content.find(word);
            // if (pos == std::string::npos)
            //     return "None";

            auto it = std::search(content.begin(), content.end(), word.begin(), word.end(), [](char x, char y)
                                  { return std::tolower(x) == std::tolower(y); });
            if (it == content.end())
                return "None";

            long long pos = std::distance(content.begin(), it);

            // 确定摘要范围
            long long start = 0;
            long long end = content.size() - 1; // 严谨一点~

            if (pos - 50 > 0) // 控制长度
                start = pos - 50;
            if (pos + 100 < end)
                end = pos + 100;

            // std::cout << pos << " " << start << " " << end << std::endl;
            if (start > end)
                return "start > end ? ";

            // 切分字符串
            return content.substr(start, end - start);
        }
    };
}
