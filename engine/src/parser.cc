#include "util.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include "log.hpp"

namespace fs = std::filesystem;

const std::string md_src_path = "../../data/raw/md";         // md 文档目录
const std::string output = "../../data/raw.txt";    // 解析完的内容

typedef struct DocInfo
{
    std::string title;   // 文档标题
    std::string content; // 文档内容
    std::string url;     // 文档来源路径
} DocInfo_t;

// 枚举指定目录下特定扩展名的文件
bool EnumFile(const std::string &src_path, const std::string &ext,
              std::vector<std::string> *file_list);

// Markdown 解析
bool ParseMd(const std::vector<std::string> &file_list,
             std::vector<DocInfo_t> *results);

// 保存结果
bool SaveResults(const std::vector<DocInfo_t> &results, const std::string &output);

int main() {
    std::vector<std::string> md_files;
    if (!EnumFile(md_src_path, ".md", &md_files)) {
        LOG(LogLevel::FATAL) << "MD 文档目录不存在或为空: " + md_src_path;
        return 1;
    }

    std::vector<DocInfo_t> results;
    if (!ParseMd(md_files, &results)) {
        LOG(LogLevel::FATAL) << "ParseMd fail";
        return 2;
    }

    LOG(LogLevel::INFO) << "MD 文档解析完成: " + std::to_string(results.size()) + " 篇文档";

    if (!SaveResults(results, output)) {
        LOG(LogLevel::FATAL) << "SaveResults fail";
        return 3;
    }

    LOG(LogLevel::INFO) << "全部完成";
    return 0;
}

bool EnumFile(const std::string &src_path, const std::string &ext,
              std::vector<std::string> *file_list) {
    fs::path root_path(src_path);
    if (!fs::exists(root_path) || !fs::is_directory(root_path)) {
        return false;
    }
    for (const auto &entry : fs::recursive_directory_iterator(root_path)) {
        if (!fs::is_regular_file(entry)) {
            continue;
        }
        if (entry.path().extension() != ext) {
            continue;
        }
        file_list->push_back(entry.path().string());
    }
    return !file_list->empty();
}

// ========== Markdown 解析 ==========

// 从 MD 内容提取标题：先检查 YAML front matter 中的 title 字段，再取第一个 # 标题
static bool ParseMdTitle(const std::string &content, std::string *title) {
    std::istringstream stream(content);
    std::string line;

    // 检查 YAML front matter（--- 开头）
    if (std::getline(stream, line) && line == "---") {
        while (std::getline(stream, line)) {
            if (line == "---") {
                break;
            }
            if (line.substr(0, 6) == "title:") {
                *title = line.substr(6);
                while (!title->empty() && (title->front() == ' ' || title->front() == '"' || title->front() == '\'')) {
                    title->erase(0, 1);
                }
                while (!title->empty() && (title->back() == ' ' || title->back() == '"' || title->back() == '\'')) {
                    title->pop_back();
                }
                if (!title->empty()) return true;
            }
        }
    } else {
        stream.clear();
        stream.seekg(0);
    }

    // 查找第一个 # 标题
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (line.size() > 2 && line[0] == '#') {
            size_t start = line.find_first_not_of('#');
            if (start != std::string::npos && line[start] == ' ') {
                *title = line.substr(start + 1);
                while (!title->empty() && title->back() == ' ') {
                    title->pop_back();
                }
                return !title->empty();
            }
        }
    }
    return false;
}

// 去除 Markdown 格式标记，提取纯文本
static std::string StripMarkdown(const std::string &content) {
    std::string text;
    text.reserve(content.size());

    bool in_code_block = false;
    bool in_front_matter = false;
    bool front_matter_done = false;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // 处理 YAML front matter
        if (!front_matter_done && line == "---") {
            if (!in_front_matter) {
                in_front_matter = true;
                continue;
            } else {
                in_front_matter = false;
                front_matter_done = true;
                continue;
            }
        }
        if (in_front_matter) {
            continue;
        }

        // 代码块开关
        if (line.substr(0, 3) == "```") {
            in_code_block = !in_code_block;
            continue;
        }
        if (in_code_block) {
            text += line + " ";
            continue;
        }

        // 跳过图片行
        static std::regex img_re(R"(!\[.*?\]\(.*?\))");
        line = std::regex_replace(line, img_re, "");

        // 去除标题前缀
        if (!line.empty() && line[0] == '#') {
            size_t start = line.find_first_not_of('#');
            if (start != std::string::npos && line[start] == ' ') {
                line = line.substr(start + 1);
            }
        }

        // 去除行内格式
        static std::regex bold_re(R"(\*\*(.+?)\*\*)");
        static std::regex italic_re(R"(\*(.+?)\*)");
        static std::regex code_re(R"(`([^`]+)`)");
        static std::regex link_re(R"(\[([^\]]+)\]\([^\)]+\))");
        static std::regex html_tag_re(R"(<[^>]+>)");

        line = std::regex_replace(line, bold_re, "$1");
        line = std::regex_replace(line, italic_re, "$1");
        line = std::regex_replace(line, code_re, "$1");
        line = std::regex_replace(line, link_re, "$1");
        line = std::regex_replace(line, html_tag_re, "");

        // 去除列表标记
        if (!line.empty() && (line[0] == '-' || line[0] == '*' || line[0] == '+')) {
            size_t pos = line.find_first_not_of("-*+ \t");
            if (pos != std::string::npos) {
                line = line.substr(pos);
            }
        } else if (!line.empty() && std::isdigit(line[0])) {
            size_t dot = line.find(". ");
            if (dot != std::string::npos && dot < 4) {
                line = line.substr(dot + 2);
            }
        }

        // 去除引用前缀
        if (!line.empty() && line[0] == '>') {
            size_t pos = line.find_first_not_of("> \t");
            if (pos != std::string::npos) {
                line = line.substr(pos);
            }
        }

        if (!line.empty()) {
            text += line + " ";
        }
    }

    return text;
}

// 从文件路径生成文档 URL
static std::string MdFileToUrl(const std::string &file_name) {
    std::string relative = file_name.substr(md_src_path.size());
    if (!relative.empty() && relative[0] == '/') {
        relative = relative.substr(1);
    }
    return relative;
}

bool ParseMd(const std::vector<std::string> &file_list,
             std::vector<DocInfo_t> *results) {
    for (const std::string &file_name : file_list) {
        std::string raw;
        if (!File_Util::ReadFileLines(file_name, &raw)) {
            continue;
        }
        DocInfo_t doc;
        if (!ParseMdTitle(raw, &doc.title)) {
            fs::path p(file_name);
            doc.title = p.stem().string();
        }
        doc.content = StripMarkdown(raw);
        if (doc.content.empty()) {
            continue;
        }
        doc.url = MdFileToUrl(file_name);
        results->emplace_back(doc);
    }
    return true;
}

// ========== 保存结果 ==========

bool SaveResults(const std::vector<DocInfo_t> &results, const std::string &output) {
    fs::path output_path(output);
    if (output_path.has_parent_path()) {
        fs::create_directories(output_path.parent_path());
    }

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
