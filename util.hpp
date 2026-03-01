#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include "cppjieba/Jieba.hpp"

class File_Util {
  public:
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
};

class String_Util {
  public:
    static void Split(std::string &line, std::vector<std::string> *result,
                      const std::string &sep) {
        boost::split(*result, line, boost::is_any_of(sep),
                     boost::algorithm::token_compress_on);
    }
};

class Jieba_util {
  private:
    static cppjieba::Jieba jieba;

  public:
    static void CutString(const std::string &src, std::vector<std::string> *out) {
        jieba.CutForSearch(src, *out);
    }
};

cppjieba::Jieba Jieba_util::jieba;
