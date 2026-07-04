#pragma once

#include "cppjieba/Jieba.hpp"

#include <filesystem>
#include <string>
#include <vector>

class File_Util {
  public:
    static std::string PathToUtf8(const std::filesystem::path &path);

    static bool ReadFile(const std::string &file_name, std::string *out);

    static bool ReadFileLines(const std::filesystem::path &file_path, std::string *out);

    // 读取文件并保留换行符，适用于 Markdown 等需要按行解析的文本格式。
    static bool ReadFileLines(const std::string &file_name, std::string *out);
};

// 字符串工具类：提供字符串切分和轻量文本归一化能力。
class String_Util {
  public:
    // 按分隔符拆分字符串，连续分隔符会忽略空 token，保持 raw.txt 字段解析稳定。
    static void Split(std::string &line, std::vector<std::string> *result, const std::string &sep);

    static void ToLowerAscii(std::string *word);
};

// cppjieba 分词工具类：隐藏词典对象的初始化细节，调用方只关心搜索模式分词。
class Jieba_util {
  private:
    static cppjieba::Jieba jieba;

  public:
    static void CutString(const std::string &src, std::vector<std::string> *out);
};
