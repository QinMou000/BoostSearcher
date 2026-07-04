#pragma once
#include "cppjieba/Jieba.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

class File_Util {
  public:
    static std::string PathToUtf8(const std::filesystem::path &path) {
        auto value = path.u8string();
        return std::string(value.begin(), value.end());
    }

    static bool ReadFile(const std::string &file_name, std::string *out) {
        std::ifstream in(file_name, std::ios::in);
        if (!in.is_open()) {
            std::cerr << "open file: " << file_name << " fail" << std::endl;
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            *out += line;
        }
        in.close();
        return true;
    }

    static bool ReadFileLines(const std::filesystem::path &file_path,
                              std::string *out) {
        if (out == nullptr) {
            return false;
        }
        out->clear();

        std::ifstream in(file_path, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            std::cerr << "open file: " << PathToUtf8(file_path) << " fail"
                      << std::endl;
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        *out = ss.str();
        in.close();
        return true;
    }

    // 读取文件并保留换行符（适用于 Markdown 等需要按行解析的格式）
    static bool ReadFileLines(const std::string &file_name, std::string *out) {
        if (out == nullptr) {
            return false;
        }
        return ReadFileLines(std::filesystem::path(file_name), out);
    }
};

// 字符串工具类：提供字符串分割和轻量文本归一化能力
class String_Util {
  public:
    // 按分隔符拆分字符串，连续分隔符视为一个（token_compress_on 行为）
    static void Split(std::string &line, std::vector<std::string> *result,
                      const std::string &sep) {
        result->clear();
        std::string::size_type start = 0;
        std::string::size_type pos = line.find(sep, start);
        while (pos != std::string::npos) {
            std::string token = line.substr(start, pos - start);
            if (!token.empty()) {
                result->emplace_back(std::move(token));
            }
            start = pos + sep.size();
            pos = line.find(sep, start);
        }
        // 处理最后一段
        std::string last = line.substr(start);
        if (!last.empty()) {
            result->emplace_back(std::move(last));
        }
    }

    static void ToLowerAscii(std::string *word) {
        if (word == nullptr) {
            return;
        }
        // 只折叠 ASCII 大写字母，避免把中文 UTF-8 字节传给 std::tolower 产生未定义行为。
        // 这个工具用于索引和查询两侧保持英文协议名大小写不敏感，同时不改动中文内容。
        for (char &ch : *word) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
    }
};

// cppjieba 分词工具类：封装分词库，提供搜索模式分词
class Jieba_util {
  private:
    // 词典路径相对于可执行文件位置，通过 CMake 定义 DICT_DIR 宏传入
#ifndef DICT_DIR
#define DICT_DIR "./dict"
#endif
    static cppjieba::Jieba jieba;

  public:
    static void CutString(const std::string &src,
                          std::vector<std::string> *out) {
        jieba.CutForSearch(src, *out);
    }
};

// 传入各词典文件的完整路径（DICT_DIR 由 CMake 传入绝对路径）
cppjieba::Jieba Jieba_util::jieba(DICT_DIR "/jieba.dict.utf8",
                                  DICT_DIR "/hmm_model.utf8",
                                  DICT_DIR "/user.dict.utf8",
                                  DICT_DIR "/idf.utf8",
                                  DICT_DIR "/stop_words.utf8");
