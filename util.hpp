#pragma once
#include <iostream>
#include <string>
#include <fstream>

class File_Util
{
public:
    static bool ReadFile(const std::string file_name, std::string *out)
    {
        std::ifstream in(file_name, std::ios::in); // 以输入模式打开 file_name 文件
        if (!in.is_open())
        {
            std::cerr << "open file error " << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(in, line)) // getline 返回的是一个引用 被重载了强制类型转化
            *out += line;              // 每读取一行 加到 out 后面

        in.close();
        return true;
    }
};