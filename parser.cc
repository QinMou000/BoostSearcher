#include "util.hpp"
#include <boost/filesystem.hpp>
#include <iostream>
#include <string>
#include <vector>
#include "log.hpp"

const std::string src_path = "./data/input";          // html 文档
const std::string output = "./data/raw_html/raw.txt"; // 解析完的内容

typedef struct DocInfo
{
    std::string title;   // 文档标题
    std::string content; // 文档内容
    std::string url;     // 在官网中的 url
} DocInfo_t;

// const & 输入
// * 输出
// & 输入输出

bool EnumFile(const std::string &src_path, std::vector<std::string> *file_list);
bool ParseHtml(const std::vector<std::string> &file_list,
               std::vector<DocInfo_t> *results);
bool SaveHtml(const std::vector<DocInfo_t> &results, const std::string &output);

int main() {
    std::vector<std::string> file_list;
    if (!EnumFile(src_path, &file_list)) {
        LOG(LogLevel::FATAL) << "EnumFile fail";
        return 1;
    }
    std::vector<DocInfo_t> results;
    if (!ParseHtml(file_list, &results)) {
        LOG(LogLevel::FATAL) << "ParseHtml fail";
        return 2;
    }
    if (!SaveHtml(results, output)) {
        LOG(LogLevel::FATAL) << "SaveHtml fail";
        return 3;
    }
    return 0;
}

bool EnumFile(const std::string &src_path, std::vector<std::string> *file_list) {
    namespace fs = boost::filesystem;
    fs::path root_path(src_path);
    if (!fs::exists(root_path)) {
        LOG(LogLevel::FATAL) << "Source file path error";
        return false;
    }
    fs::recursive_directory_iterator end;
    for (fs::recursive_directory_iterator it(root_path); it != end; it++) {
        if (!fs::is_regular_file(*it)) {
            continue;
        }
        if (it->path().extension() != ".html") {
            continue;
        }
        file_list->push_back(it->path().string());
    }
    return true;
}

static bool ParseTitle(const std::string &result, std::string *title) {
    std::size_t begin = result.find("<title>");
    if (begin == std::string::npos) {
        return false;
    }
    std::size_t end = result.find("</title>");
    if (end == std::string::npos) {
        return false;
    }
    begin += std::string("<title>").size();
    if (begin > end) {
        return false;
    }
    *title = result.substr(begin, end - begin);
    return true;
}

// 简易状态机提取 HTML 正文，去除所有标签
static bool ParseContent(const std::string &result, std::string *content) {
    enum Status { CONTENT, LABEL };
    Status s = LABEL;
    for (char c : result) {
        switch (s) {
            case LABEL:
                if (c == '>') {
                    s = CONTENT;
                }
                break;
            case CONTENT:
                if (c == '<') {
                    s = LABEL;
                } else {
                    // 换行符替换为空格，后续用 \n 做文档间分隔
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

static bool ParseUrl(const std::string &file_name, std::string *url) {
    const std::string head = "https://www.boost.org/doc/libs/latest/doc/html";
    std::string tail = file_name.substr(src_path.size());
    *url = head + tail;
    return true;
}

bool ParseHtml(const std::vector<std::string> &file_list,
               std::vector<DocInfo_t> *results) {
    for (const std::string &file_name : file_list) {
        std::string result;
        if (!File_Util::ReadFile(file_name, &result)) {
            continue;
        }
        DocInfo_t doc;
        if (!ParseTitle(result, &doc.title)) {
            continue;
        }
        if (!ParseContent(result, &doc.content)) {
            continue;
        }
        if (!ParseUrl(file_name, &doc.url)) {
            continue;
        }
        results->emplace_back(doc);
    }
    return true;
}

bool SaveHtml(const std::vector<DocInfo_t> &results, const std::string &output) {
    constexpr const char *kSep = "\3";
    std::ofstream out(output, std::ios::out | std::ios::binary);
    if (!out.is_open()) {
        LOG(LogLevel::FATAL) << "open file " + output + " fail";
        return false;
    }
    for (auto &it : results) {
        std::string out_line;
        out_line = it.title;
        out_line += kSep;
        out_line += it.content;
        out_line += kSep;
        out_line += it.url;
        out_line += "\n";
        out.write(out_line.c_str(), out_line.size());
    }
    out.close();
    return true;
}
