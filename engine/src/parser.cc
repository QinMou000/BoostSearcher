#include "util.hpp"
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <regex>
#include "log.hpp"

namespace fs = std::filesystem;

const std::string md_src_path = "./data/raw/md"; // md 文档目录
const std::string output = "./data/raw.txt";     // 解析完的内容

typedef struct DocInfo
{
    std::string title;   // 文档标题
    std::string content; // 文档内容
    std::string url;     // 文档来源路径
} DocInfo_t;

// 枚举指定目录下特定扩展名的文件
bool EnumFile(const std::string &src_path, const std::string &ext,
              std::vector<fs::path> *file_list);

// Markdown 解析
bool ParseMd(const std::vector<fs::path> &file_list,
             std::vector<DocInfo_t> *results);

// 保存结果
bool SaveResults(const std::vector<DocInfo_t> &results, const std::string &output);

int main()
{
    try
    {
        LOG(LogLevel::INFO) << "parser start, source: " + md_src_path + ", output: " + output;

        std::vector<fs::path> md_files;
        if (!EnumFile(md_src_path, ".md", &md_files))
        {
            LOG(LogLevel::FATAL) << "MD 文档目录不存在或为空: " + md_src_path;
            return 1;
        }
        LOG(LogLevel::INFO) << "MD 文档枚举完成: " + std::to_string(md_files.size()) + " 个文件";

        std::vector<DocInfo_t> results;
        if (!ParseMd(md_files, &results))
        {
            LOG(LogLevel::FATAL) << "ParseMd fail";
            return 2;
        }

        LOG(LogLevel::INFO) << "MD 文档解析完成: " + std::to_string(results.size()) + " 篇文档";

        if (!SaveResults(results, output))
        {
            LOG(LogLevel::FATAL) << "SaveResults fail";
            return 3;
        }

        LOG(LogLevel::INFO) << "全部完成";
        return 0;
    }
    catch (const std::exception &e)
    {
        LOG(LogLevel::FATAL) << std::string("parser unexpected exception: ") + e.what();
        return 4;
    }
    catch (...)
    {
        LOG(LogLevel::FATAL) << "parser unknown exception";
        return 5;
    }
}

bool EnumFile(const std::string &src_path, const std::string &ext,
              std::vector<fs::path> *file_list)
{
    if (file_list == nullptr)
    {
        LOG(LogLevel::ERROR) << "EnumFile output list is null";
        return false;
    }
    file_list->clear();

    if (src_path.empty())
    {
        LOG(LogLevel::ERROR) << "EnumFile source path is empty";
        return false;
    }
    if (ext.empty())
    {
        LOG(LogLevel::ERROR) << "EnumFile extension is empty";
        return false;
    }

    try
    {
        fs::path root_path(src_path);
        if (!fs::exists(root_path))
        {
            LOG(LogLevel::ERROR) << "EnumFile source path does not exist: " + src_path;
            return false;
        }
        if (!fs::is_directory(root_path))
        {
            LOG(LogLevel::ERROR) << "EnumFile source path is not a directory: " + src_path;
            return false;
        }

        for (const auto &entry : fs::recursive_directory_iterator(root_path))
        {
            if (!fs::is_regular_file(entry))
            {
                continue;
            }
            if (entry.path().extension() != fs::path(ext))
            {
                continue;
            }
            file_list->push_back(entry.path());
        }
    }
    catch (const fs::filesystem_error &e)
    {
        LOG(LogLevel::ERROR) << std::string("EnumFile filesystem error: ") + e.what();
        return false;
    }
    catch (const std::exception &e)
    {
        LOG(LogLevel::ERROR) << std::string("EnumFile error: ") + e.what();
        return false;
    }

    if (file_list->empty())
    {
        LOG(LogLevel::WARNING) << "EnumFile found no files, source: " + src_path + ", ext: " + ext;
        return false;
    }
    return true;
}

// ========== Markdown 解析 ==========

// 从 MD 内容提取标题：先检查 YAML front matter 中的 title 字段，再取第一个 # 标题
static bool ParseMdTitle(const std::string &content, std::string *title)
{
    std::istringstream stream(content);
    std::string line;

    // 检查 YAML front matter（--- 开头）
    if (std::getline(stream, line) && line == "---")
    {
        while (std::getline(stream, line))
        {
            if (line == "---")
            {
                break;
            }
            if (line.substr(0, 6) == "title:")
            {
                *title = line.substr(6);
                while (!title->empty() && (title->front() == ' ' || title->front() == '"' || title->front() == '\''))
                {
                    title->erase(0, 1);
                }
                while (!title->empty() && (title->back() == ' ' || title->back() == '"' || title->back() == '\''))
                {
                    title->pop_back();
                }
                if (!title->empty())
                    return true;
            }
        }
    }
    else
    {
        stream.clear();
        stream.seekg(0);
    }

    // 查找第一个 # 标题
    while (std::getline(stream, line))
    {
        if (line.empty())
            continue;
        if (line.size() > 2 && line[0] == '#')
        {
            size_t start = line.find_first_not_of('#');
            if (start != std::string::npos && line[start] == ' ')
            {
                *title = line.substr(start + 1);
                while (!title->empty() && title->back() == ' ')
                {
                    title->pop_back();
                }
                return !title->empty();
            }
        }
    }
    return false;
}

// 去除 Markdown 格式标记，提取纯文本
static std::string StripMarkdown(const std::string &content)
{
    std::string text;
    text.reserve(content.size());

    bool in_code_block = false;
    bool in_front_matter = false;
    bool front_matter_done = false;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line))
    {
        // 处理 YAML front matter
        if (!front_matter_done && line == "---")
        {
            if (!in_front_matter)
            {
                in_front_matter = true;
                continue;
            }
            else
            {
                in_front_matter = false;
                front_matter_done = true;
                continue;
            }
        }
        if (in_front_matter)
        {
            continue;
        }

        // 代码块开关
        if (line.substr(0, 3) == "```")
        {
            in_code_block = !in_code_block;
            continue;
        }
        if (in_code_block)
        {
            text += line + " ";
            continue;
        }

        // 跳过图片行
        static std::regex img_re(R"(!\[.*?\]\(.*?\))");
        line = std::regex_replace(line, img_re, "");

        // 去除标题前缀
        if (!line.empty() && line[0] == '#')
        {
            size_t start = line.find_first_not_of('#');
            if (start != std::string::npos && line[start] == ' ')
            {
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
        if (!line.empty() && (line[0] == '-' || line[0] == '*' || line[0] == '+'))
        {
            size_t pos = line.find_first_not_of("-*+ \t");
            if (pos != std::string::npos)
            {
                line = line.substr(pos);
            }
        }
        else if (!line.empty() && std::isdigit(static_cast<unsigned char>(line[0])))
        {
            size_t dot = line.find(". ");
            if (dot != std::string::npos && dot < 4)
            {
                line = line.substr(dot + 2);
            }
        }

        // 去除引用前缀
        if (!line.empty() && line[0] == '>')
        {
            size_t pos = line.find_first_not_of("> \t");
            if (pos != std::string::npos)
            {
                line = line.substr(pos);
            }
        }

        if (!line.empty())
        {
            text += line + " ";
        }
    }

    return text;
}

static std::string PathToUtf8(const fs::path &path)
{
    return File_Util::PathToUtf8(path);
}

static std::string PathToGenericUtf8(const fs::path &path)
{
    auto value = path.generic_u8string();
    return std::string(value.begin(), value.end());
}

// 从文件路径生成文档 URL
static std::string MdFileToUrl(const fs::path &file_name)
{
    std::error_code ec;
    fs::path relative = fs::relative(file_name, fs::path(md_src_path), ec);
    if (ec || relative.empty())
    {
        LOG(LogLevel::WARNING) << "MD file path cannot be made relative: " + PathToUtf8(file_name);
        return PathToGenericUtf8(file_name.filename());
    }

    return PathToGenericUtf8(relative);
}

bool ParseMd(const std::vector<fs::path> &file_list,
             std::vector<DocInfo_t> *results)
{
    if (results == nullptr)
    {
        LOG(LogLevel::ERROR) << "ParseMd results is null";
        return false;
    }
    results->clear();

    if (file_list.empty())
    {
        LOG(LogLevel::ERROR) << "ParseMd file list is empty";
        return false;
    }

    size_t read_fail_count = 0;
    size_t empty_content_count = 0;
    size_t fallback_title_count = 0;

    for (const fs::path &file_name : file_list)
    {
        if (file_name.empty())
        {
            LOG(LogLevel::WARNING) << "ParseMd skip empty file path";
            continue;
        }

        std::string raw;
        if (!File_Util::ReadFileLines(file_name, &raw))
        {
            ++read_fail_count;
            LOG(LogLevel::WARNING) << "ParseMd read file fail, skip: " + PathToUtf8(file_name);
            continue;
        }
        if (raw.empty())
        {
            ++empty_content_count;
            LOG(LogLevel::WARNING) << "ParseMd skip empty file: " + PathToUtf8(file_name);
            continue;
        }

        DocInfo_t doc;
        if (!ParseMdTitle(raw, &doc.title))
        {
            doc.title = PathToUtf8(file_name.stem());
            ++fallback_title_count;
            LOG(LogLevel::WARNING) << "ParseMd title not found, fallback to filename: " + PathToUtf8(file_name);
        }
        doc.content = StripMarkdown(raw);
        if (doc.content.empty())
        {
            ++empty_content_count;
            LOG(LogLevel::WARNING) << "ParseMd skip file with empty content after strip: " + PathToUtf8(file_name);
            continue;
        }
        doc.url = MdFileToUrl(file_name);
        results->emplace_back(doc);
    }

    LOG(LogLevel::INFO) << "ParseMd summary, input: " + std::to_string(file_list.size()) + ", parsed: " + std::to_string(results->size()) + ", read_fail: " + std::to_string(read_fail_count) + ", empty_content: " + std::to_string(empty_content_count) + ", fallback_title: " + std::to_string(fallback_title_count);

    return !results->empty();
}

// ========== 保存结果 ==========

bool SaveResults(const std::vector<DocInfo_t> &results, const std::string &output)
{
    if (output.empty())
    {
        LOG(LogLevel::ERROR) << "SaveResults output path is empty";
        return false;
    }
    if (results.empty())
    {
        LOG(LogLevel::ERROR) << "SaveResults results is empty";
        return false;
    }

    try
    {
        fs::path output_path(output);
        if (output_path.has_parent_path())
        {
            fs::create_directories(output_path.parent_path());
        }
    }
    catch (const fs::filesystem_error &e)
    {
        LOG(LogLevel::ERROR) << std::string("SaveResults create output directory fail: ") + e.what();
        return false;
    }

    constexpr const char *kSep = "\3";
    std::ofstream out(output, std::ios::out | std::ios::binary);
    if (!out.is_open())
    {
        LOG(LogLevel::FATAL) << "open file " + output + " fail";
        return false;
    }
    for (const auto &it : results)
    {
        std::string out_line;
        out_line = it.title;
        out_line += kSep;
        out_line += it.content;
        out_line += kSep;
        out_line += it.url;
        out_line += "\n";
        out.write(out_line.c_str(), out_line.size());
        if (!out.good())
        {
            LOG(LogLevel::ERROR) << "SaveResults write fail, output: " + output + ", url: " + it.url;
            return false;
        }
    }
    out.close();
    if (!out.good())
    {
        LOG(LogLevel::ERROR) << "SaveResults close fail, output: " + output;
        return false;
    }

    LOG(LogLevel::INFO) << "SaveResults success, output: " + output + ", docs: " + std::to_string(results.size());
    return true;
}
