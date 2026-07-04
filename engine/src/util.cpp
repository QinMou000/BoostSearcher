#include "util.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

#ifndef DICT_DIR
#define DICT_DIR "./dict"
#endif

std::string File_Util::PathToUtf8(const std::filesystem::path &path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

bool File_Util::ReadFile(const std::string &file_name, std::string *out) {
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

bool File_Util::ReadFileLines(const std::filesystem::path &file_path, std::string *out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();

    std::ifstream in(file_path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "open file: " << PathToUtf8(file_path) << " fail" << std::endl;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    in.close();
    return true;
}

bool File_Util::ReadFileLines(const std::string &file_name, std::string *out) {
    if (out == nullptr) {
        return false;
    }
    return ReadFileLines(std::filesystem::path(file_name), out);
}

void String_Util::Split(std::string &line, std::vector<std::string> *result, const std::string &sep) {
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

    std::string last = line.substr(start);
    if (!last.empty()) {
        result->emplace_back(std::move(last));
    }
}

void String_Util::ToLowerAscii(std::string *word) {
    if (word == nullptr) {
        return;
    }
    // 只折叠 ASCII 大写字母，避免把中文 UTF-8 字节传给 std::tolower 产生未定义行为。
    for (char &ch : *word) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
}

void Jieba_util::CutString(const std::string &src, std::vector<std::string> *out) { jieba.CutForSearch(src, *out); }

cppjieba::Jieba Jieba_util::jieba(DICT_DIR "/jieba.dict.utf8", DICT_DIR "/hmm_model.utf8", DICT_DIR "/user.dict.utf8",
                                  DICT_DIR "/idf.utf8", DICT_DIR "/stop_words.utf8");
