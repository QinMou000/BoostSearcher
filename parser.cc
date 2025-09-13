#include <iostream>
#include <vector>
#include <string>

const std::string src_path = "./data/input";          //
const std::string output = "./data/raw_html/raw.txt"; //

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
bool ParseHtml(const std::vector<std::string> &file_list, std::vector<DocInfo_t> *results);
bool SaveHtml(const std::vector<DocInfo_t> &results, const std::string &output);

int main()
{
    std::vector<std::string> file_list;
    // 1. 递归拿到 html 文件名 带路径保存到输出型参数中
    if (!EnumFile(src_path, &file_list))
    {
        std::cerr << "EnumFile fail " << std::endl;
        return 1;
    }
    // 2. 从 file_list 读取每个文件的内容并解析
    std::vector<DocInfo_t> results;
    if (!ParseHtml(file_list, &results))
    {
        std::cerr << "ParseHtml fail " << std::endl;
        return 2;
    }
    // 3. 将解析完的文件内容写入到output 分隔符为 "\3" ascll码中的不可显字符都可
    if (!SaveHtml(results, output))
    {
        std::cerr << "SaveHtml fail " << std::endl;
        return 3;
    }
    return 0;
}

bool EnumFile(const std::string &src_path, std::vector<std::string> *file_list)
{
    return true;
}

bool ParseHtml(const std::vector<std::string> &file_list, std::vector<DocInfo_t> *results)
{
    return true;
}

bool SaveHtml(const std::vector<DocInfo_t> &results, const std::string &output)
{
    return true;
}
