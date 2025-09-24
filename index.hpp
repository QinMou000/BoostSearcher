#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include "util.hpp"

namespace ns_index
{
    struct DocInfo
    {
        std::string title;   // 文档标题
        std::string content; // 文档内容
        std::string url;     // 文档地址
        uint64_t doc_id;     // 文档 id
    };

    struct InvertedElem
    {
        uint64_t doc_id;
        std::string word;
        int weight; // 权重
    };

    typedef std::vector<InvertedElem> InvertedList; // 倒排拉链

    class index
    {
    private:
        std::vector<DocInfo> forward_index;                           // 正排索引 用数组下表作为文档 id
        std::unordered_map<std::string, InvertedList> inverted_index; // 倒排索引 一个 word 和一组(almost) InvertedList 对应 (关键字和倒排拉链的映射 )

    public:
        index() {}
        ~index() {}

        // 根据 doc_id 找到文档
        DocInfo *GetForwardIndex(uint64_t doc_id)
        {
            // 判断 doc_id 是否超出范围
            if (doc_id >= forward_index.size())
            {
                std::cerr << "doc_id out of range " << std::endl;
                return nullptr;
            }
            return &forward_index[doc_id];
        }

        // 根据 word 找到倒排拉链
        InvertedList *GetInvertedList(std::string &word)
        {
            auto ret = inverted_index.find(word);
            if (ret == inverted_index.end())
            {
                std::cerr << "can't find this word: " << word << " in inverted_index" << std::endl;
                return nullptr;
            }
            return &ret->second;
        }

        // 根据清洗后的数据(boost-searcher/data/raw_html/raw.txt) 构建正排倒排索引
        bool BuildIndex(const std::string &raw_file_path)
        {
            // 打开文件
            std::ifstream in(raw_file_path, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                std::cerr << "open file : " << raw_file_path << " fail " << std::endl;
                return false;
            }

            std::string line;
            while (std::getline(in, line)) // 文件按行读取
            {
                DocInfo *doc = BuildForwardIndex(line);
                if (!doc)
                {
                    std::cerr << "BuildFowardIndex " << line << "fail" << std::endl;
                    return false;
                }
                BuildInvertedIndex(*doc);
            }
        }

    private:
        /*
            title\3content\3url\n title\3content\3url\n title\3content\3url\n
        */
        DocInfo *BuildForwardIndex(std::string &line)
        {
            // 根据分隔符 拆分成几个小字符串
            const std::string sep = "\3";
            std::vector<std::string> result;
            String_Util::Split(line, &result, sep);

            // 写入 DocInfo
            DocInfo doc;
            doc.title = result[0];             // title
            doc.content = result[1];           // content
            doc.url = result[2];               // url
            doc.doc_id = forward_index.size(); // 很巧妙 恰好用插入前正排索引数组的大小来当 doc_id

            // push_back 到 forward_index 里面
            forward_index.emplace_back(doc);

            return &forward_index.back(); // TODO 返回指针有一定的风险 vector扩容后 可能造成野指针 但是即拿即用没什么问题 (maybe~)
        }
        bool BuildInvertedIndex(const DocInfo doc)
        {
            struct word_count
            {
                int title_count = 0;
                int conten_count = 0;
            };

            std::unordered_map<std::string, word_count> word_map;

            std::vector<std::string> title_words;
            Jieba_util::CutString(doc.title, &title_words); // 对标题进行切分
            for (auto word : title_words)
            {
                boost::to_lower(word);        // 将词统一转成小写
                word_map[word].title_count++; // 插入词和出现次数的映射表中
            }
            // 同理将内容的次数统计好
            std::vector<std::string> content_words;
            Jieba_util::CutString(doc.content, &content_words);
            for (auto word : content_words)
            {
                boost::to_lower(word);
                word_map[word].title_count++;
            }
#define TITLE_W 10
#define CONTENT_W 1
            for (auto &pair : word_map) // 这里是每一个词和他在这个文档中出现次数的映射
            {
                // 我们要将这个得到一个倒排拉链里面的一个元素
                InvertedElem inverted_elem;
                inverted_elem.doc_id = doc.doc_id; // 这里就将DocInfo中的id用上了
                inverted_elem.word = pair.first;
                inverted_elem.weight = pair.second.title_count * TITLE_W +
                                       pair.second.conten_count * CONTENT_W;
                // 从倒排索引中拿到当前词的倒排拉链的引用
                InvertedList &inverted_list = inverted_index[pair.first];
                inverted_list.emplace_back(inverted_elem);
            }
            return true;
        }
    };
};