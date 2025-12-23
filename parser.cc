/*
 * @Author: wang-qin928 2830862261@qq.com
 * @Date: 2025-10-09 21:52:01
 * @LastEditors: wang-qin928 2830862261@qq.com
 * @LastEditTime: 2025-12-23 11:27:12
 * @FilePath: \boost-searcher\parser.cc
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "util.hpp"
#include <boost/filesystem.hpp>
#include <iostream>
#include <string>
#include <vector>
#include "log.hpp"

const std::string src_path = "./data/input";          // html 文档
const std::string output = "./data/raw_html/raw.txt"; // 解析完的内容

typedef struct DocInfo {
    std::string title;   // 文档标题
    std::string content; // 文档内容
    std::string url;     // 在官网中的 url
} DocInfo_t;

// const & 输入
// * 输出
// & 输入输出

bool EnumFile(const std::string &src_path, std::vector<std::string> *file_list);
bool ParseHtml(const std::vector<std::string> &file_list, std::vector<DocInfo_t> *results);
bool SaveHtml(const std::vector<DocInfo_t> &results, const std::string &output);

int main() {
    std::vector<std::string> file_list;
    // 1. 递归拿到 html 文件名 带路径保存到输出型参数中
    if (!EnumFile(src_path, &file_list)) {
        // std::cerr << "EnumFile fail " << std::endl;
        LOG(FATAL, "EnumFile fail ");
        return 1;
    }
    // 2. 从 file_list 读取每个文件的内容并解析
    std::vector<DocInfo_t> results;
    if (!ParseHtml(file_list, &results)) {
        // std::cerr << "ParseHtml fail " << std::endl;
        LOG(FATAL, "ParseHtml fail ");
        return 2;
    }
    // 3. 将解析完的文件内容写入到output 分隔符为 "\3" ascll码中的不可显字符都可
    if (!SaveHtml(results, output)) {
        // std::cerr << "SaveHtml fail " << std::endl;
        LOG(FATAL, "SaveHtml fail ");
        return 3;
    }
    return 0;
}

bool EnumFile(const std::string &src_path, std::vector<std::string> *file_list) {
    namespace fs = boost::filesystem;
    // 判断路径是否存在
    fs::path root_path(src_path);
    if (!fs::exists(root_path)) {
        // std::cerr << "Source file path error " << std::endl;
        LOG(FATAL, "Source file path error ");
        return false;
    }
    // 定义一个空迭代器 用于判断遍历结束
    fs::recursive_directory_iterator end; // 空迭代器
    // 开始用 boost 库中的迭代器遍历
    for (fs::recursive_directory_iterator it(root_path); it != end; it++) {
        // 判断文件是否为常规文件 排除目录
        if (!fs::is_regular_file(*it)) {
            continue;
        }
        // 判断文件后缀是否为 .html
        if (it->path().extension() != ".html") {
            continue;
        }
        // 输入到 file_list 中
        // std::cout << it->path().string() << std::endl;
        file_list->push_back(it->path().string());
    }

    return true;
}

// 拿到 title
static bool ParserTitle(const std::string &result, std::string *title) {
    std::size_t begin = result.find("<title>"); // 找到 title 开始位置
    if (begin == std::string::npos) {
        return false;
    }

    std::size_t end = result.find("</title>"); // 找到 title 结束位置
    if (end == std::string::npos) {
        return false;
    }

    begin += std::string("<title>").size(); // 加上偏移量

    if (begin > end) {
        return false;
    }

    *title = result.substr(begin, end - begin); // 输出结果

    return true;
}

// 拿到 content
bool ParserContent(const std::string &result, std::string *content) {
    enum status { // 简易状态机 表示当前字符属于哪一部分
        CONTENT,
        LABLE
    };
    enum status s = LABLE; // 文件开头一定是标签

    for (char c : result) {
        switch (s) {
            case LABLE:
                if (c == '>') {
                    s = CONTENT;    // 表示已经到了 正文部分
                }
                break;
            case CONTENT:
                if (c == '<') {
                    s = LABLE;    // 表示走到了标签部分
                } else {
                    // 另外 如果c等于换行符 我们需要去掉 因为我们后面也要用 \n
                    // 作为解析后的分隔符
                    if (c == '\n') {
                        c = ' ';
                    }
                    *content += c;
                }
                break;
            default:
                break;
        }
    }

    return true;
}

// 拿到 url
bool ParserUrl(const std::string &file_name, std::string *url) {
    std::string head = "https://www.boost.org/doc/libs/latest/doc/html"; // 官网部分地址

    std::string tail =
        file_name.substr(src_path.size()); // 截取文件路径后半段 拼接

    *url = head + tail;

    return true;
}

void Show(DocInfo_t doc) {
    std::cout << "title : " << doc.title << std::endl;
    std::cout << "content : " << doc.content << std::endl;
    std::cout << "url : " << doc.url << std::endl;
}

bool ParseHtml(const std::vector<std::string> &file_list, std::vector<DocInfo_t> *results) {
    for (const std::string &file_name : file_list) {
        // 读取文件
        std::string result;
        if (!File_Util::ReadFile(file_name, &result)) {
            continue;
        }
        // 拿到 title
        DocInfo_t doc;
        if (!ParserTitle(result, &doc.title)) {
            continue;
        }
        // 拿到 content
        if (!ParserContent(result, &doc.content)) {
            continue;
        }
        // 拿到 url
        if (!ParserUrl(file_name, &doc.url)) { // TODO
            continue;
        }
        // 走到这里 说明信息都已经拿到了
        results->emplace_back(doc);

        // DEBUG
        // Show(doc);
        // break;
    }
    return true;
}

bool SaveHtml(const std::vector<DocInfo_t> &results, const std::string &output) {
#define SEP "\3"
<<<<<<< HEAD
    std::ofstream out(
        output,
        std::ios::out | std::ios::binary); // 以 读 和 二进制 形式打开 output 文件
    if (!out.is_open()) {
        // std::cerr << "open file " << output << "fail" << std::endl;
        LOG(FATAL, "open file " + output + "fail");
        return false;
    }
    for (auto &it : results) {
        std::string out_line;
        out_line = it.title;
        out_line += SEP;
        out_line += it.content;
        out_line += SEP;
        out_line += it.url;
        out_line += "\n";
=======
  std::ofstream out(
      output,
      std::ios::out | std::ios::binary); // 以 读 和 二进制 形式打开 output 文件
  if (!out.is_open())
  {
    // std::cerr << "open file " << output << "fail" << std::endl;
    // LOG(FATAL, "open file " + output + "fail");
    LOG(LogLevel::FATAL) << "open file " + output + "fail";
    return false;
  }
  for (auto &it : results)
  {
    std::string out_line;
    out_line = it.title;
    out_line += SEP;
    out_line += it.content;
    out_line += SEP;
    out_line += it.url;
    out_line += "\n";
>>>>>>> 77b05d638f516fb3723e1d688b0ef4da4a9a5b5c

        out.write(out_line.c_str(), out_line.size());
    }
    out.close();

    return true;
}
