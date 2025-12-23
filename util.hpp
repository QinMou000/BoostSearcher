/*
 * @Author: wang-qin928 2830862261@qq.com
 * @Date: 2025-10-09 21:52:01
 * @LastEditors: wang-qin928 2830862261@qq.com
 * @LastEditTime: 2025-12-23 11:27:18
 * @FilePath: \boost-searcher\util.hpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include "cppjieba/Jieba.hpp"
#include "log.hpp"

class File_Util {
  public:
    static bool ReadFile(const std::string file_name, std::string *out) {
        std::ifstream in(file_name, std::ios::in); // 以输入模式(读)打开 file_name 文件
        if (!in.is_open()) {
            std::cerr << "open file : " << file_name << " fail " << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(in, line)) { // getline 返回的是一个引用 被重载了强制类型转化
            *out += line;    // 每读取一行 加到 out 后面
        }

        in.close();
        return true;
    }
};

class String_Util {
  public:
    static void Split(std::string &line, std::vector<std::string> *result, const std::string &sep) {
        boost::split(*result, line, boost::is_any_of(sep), boost::algorithm::token_compress_on);
    }
};

class Jieba_util {
  private:
    static cppjieba::Jieba jieba;

  public:
    static void CutString(const std::string &src, std::vector<std::string> *out) {
        jieba.CutForSearch(src, *out); // 切词函数
    }
};

cppjieba::Jieba Jieba_util::jieba; // 类内 statci 成员在类外定义
