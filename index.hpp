#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <mutex>
#include "util.hpp"
#include "log.hpp"

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

    class Index
    {
    private:
        std::vector<DocInfo> forward_index;                           // 正排索引 用数组下表作为文档 id
        std::unordered_map<std::string, InvertedList> inverted_index; // 倒排索引 一个 word 和一组(almost) InvertedList 对应 (关键字和倒排拉链的映射 )
    private:
        Index() {}
        Index(Index &index) = delete;
        Index &operator=(const Index &index) = delete;

        static Index *instance;
        static std::mutex mtx;

    public:
        static Index *GetInstance()
        {
            // 外面再包一层 可以挡住绝大部分线程 不用让那么多线程到锁那里去排队
            if (instance == nullptr)
            {
                mtx.lock(); // 加锁
                if (instance == nullptr)
                {
                    instance = new Index;
                }
                mtx.unlock();
            }
            return instance;
        }

        ~Index() {}

        // 根据 doc_id 找到文档
        DocInfo *GetForwardIndex(uint64_t doc_id)
        {
            // 判断 doc_id 是否超出范围
            if (doc_id >= forward_index.size())
            {
                // std::cerr << "doc_id out of range " << std::endl;
                // LOG(WARNING, "doc_id out of range ");
                LOG(LogLevel::WARNING) << "doc_id out of range ";
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
                // std::cerr << "can't find this word: " << word << " in inverted_index" << std::endl;
                // LOG(WARNING, "can't find this word: " + word + " in inverted_index");
                LOG(LogLevel::WARNING) << "can't find this word: " + word + " in inverted_index";
                return nullptr;
            }
            return &ret->second;
        }

        // 根据清洗后的数据(boost-searcher/data/raw_html/raw.txt) 构建正排倒排索引
        /**
         * @brief Builds the index from a raw file.
         *
         * This function reads the specified file line by line, constructs the forward index for each line,
         * and then builds the inverted index using the resulting document information. It logs progress
         * every 50 documents processed and handles errors such as file opening failure or forward index
         * construction failure.
         *
         * @param raw_file_path The path to the raw input file containing document data.
         * @return true if the index was built successfully; false otherwise.
         */
        bool BuildIndex(const std::string &raw_file_path)
        {
            // 打开文件
            std::ifstream in(raw_file_path, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                // std::cerr << "open file : " << raw_file_path << " fail " << std::endl;
                // LOG(FATAL, "open file : " + raw_file_path + " fail ");
                LOG(LogLevel::FATAL) << "open file : " + raw_file_path + " fail ";
                return false;
            }
            int cnt = 0;
            std::string line;
            while (std::getline(in, line)) // 文件按行读取
            {
                DocInfo *doc = BuildForwardIndex(line);
                if (!doc)
                {
                    // std::cerr << "BuildFowardIndex " << line << "fail" << std::endl;
                    // LOG(ERROR, "BuildFowardIndex " + line + "fail");
                    LOG(LogLevel::ERROR) << "BuildFowardIndex " + line + "fail";
                    return false;
                }
                BuildInvertedIndex(*doc);
                cnt++;
                if (cnt % 50 == 0)
                {
                    // std::cout << "已建立索引:" << cnt << std::endl; // TODO 引入日志
                    // LOG(INFO, "已建立索引" + std::to_string(cnt));
                    LOG(LogLevel::INFO) << "已建立索引" + std::to_string(cnt);
                }
            }
            return true;
        }

    private:
        /**
         * @brief Builds a forward index entry from a line of text.
         *
         * This function splits the input line using the specified separator,
         * extracts the title, content, and URL fields, and constructs a DocInfo object.
         * The doc_id is assigned based on the current size of the forward_index vector.
         * The new DocInfo is appended to the forward_index and a pointer to it is returned.
         *
         * @param line The input line containing document information, separated by the specified delimiter.
         * @return DocInfo* Pointer to the newly created DocInfo object, or nullptr if the input is invalid.
         *
         * @note Returning a pointer to an element in the vector may be risky if the vector is resized,
         *       potentially leading to a dangling pointer. However, immediate usage is generally safe.
         */
        /*
            title\3content\3url\n title\3content\3url\n title\3content\3url\n
        */
        DocInfo *BuildForwardIndex(std::string &line)
        {
            // 根据分隔符 拆分成几个小字符串
            const std::string sep = "\3";
            std::vector<std::string> result;
            String_Util::Split(line, &result, sep);

            // 检查分割结果是否足够
            if (result.size() < 3)
            {
                // LOG(ERROR, "Split line failed, not enough fields: " + line);
                LOG(LogLevel::ERROR) << "Split line failed, not enough fields: " + line;
                return nullptr;
            }

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

        /**
         * @brief Builds the inverted index for a given document.
         *
         * This function processes the provided document by tokenizing its title and content,
         * converting all words to lowercase, and counting their occurrences separately in the title and content.
         * It then calculates a weighted score for each word based on its frequency in the title and content,
         * using predefined weights (TITLE_W and CONTENT_W). For each unique word, an inverted index element
         * is created and added to the corresponding inverted list in the inverted index.
         *
         * @param doc The document information containing doc_id, title, content, and url.
         * @return true if the inverted index was built successfully.
         */
        bool BuildInvertedIndex(const DocInfo doc)
        {
            struct word_count
            {
                int title_count = 0;
                int content_count = 0;
            };
            // TODEBUG
            // if (doc.doc_id == 8678)
            // {
            //     std::cout << doc.url << std::endl;
            //     std::cout << doc.content << std::endl;
            //     std::cout << doc.title << std::endl;
            // }
            // TODEBUG
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
                word_map[word].content_count++;
            }
#define TITLE_W 10
#define CONTENT_W 1
            // TODEBUG
            // if (doc.doc_id == 8678)
            // {
            //     std::cout << doc.url << std::endl;
            //     std::cout << doc.content << std::endl;
            //     std::cout << doc.title << std::endl;
            // }
            // TODEBUG
            for (auto &pair : word_map) // 这里是每一个词和他在这个文档中出现次数的映射
            {
                // 我们要将这个得到一个倒排拉链里面的一个元素
                InvertedElem inverted_elem;
                inverted_elem.doc_id = doc.doc_id; // 这里就将DocInfo中的id用上了
                inverted_elem.word = pair.first;
                inverted_elem.weight = pair.second.title_count * TITLE_W +
                                       pair.second.content_count * CONTENT_W;
                // 从倒排索引中拿到当前词的倒排拉链的引用
                InvertedList &inverted_list = inverted_index[pair.first];
                inverted_list.emplace_back(inverted_elem);
            }
            return true;
        }
    };
    Index *Index::instance = nullptr; // 在类外初始化单例
    std::mutex Index::mtx;
};