#include "daemon.h"
#include "httplib.h"
#include "log.h"
#include "searcher.h"
#include "util.h"
#include <algorithm>
#include <filesystem>

const std::string raw = "./data/raw.txt";
const std::string root_path = "./wwwroot";

// 统一把浏览器传来的路径分隔符转换为正斜杠，便于后续安全检查。
std::string NormalizeDocPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

// 只拦截独立的 ".." 路径段，允许文件名里出现普通连续点号。
bool HasParentPathSegment(const std::string &path) {
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        if (end - start == 2 && path[start] == '.' && path[start + 1] == '.') {
            return true;
        }
        start = end + 1;
    }
    return false;
}

// 校验文章路径只能落在 Markdown 原始目录内，防止 /doc 接口越权读文件。
bool IsSafeDocPath(const std::string &path) {
    // 禁止绝对路径和目录回退，避免 /doc 被用来读取 md 根目录外的文件。
    if (path.empty() || path[0] == '/' || HasParentPathSegment(path)) {
        return false;
    }
    if (path.rfind("data/raw/md/", 0) != 0) {
        return false;
    }
    constexpr const char *kMdExt = ".md";
    if (path.size() < 3 || path.compare(path.size() - 3, 3, kMdExt) != 0) {
        return false;
    }
    return true;
}

int main() {
    daemon();
    FileLogStrategy();

    ns_searcher::Searcher searcher;
    searcher.InitSearcher(raw);

    httplib::Server svr;
    svr.set_base_dir(root_path.c_str());

    svr.Get("/s", [&](const httplib::Request &req, httplib::Response &res) {
        if (!req.has_param("word")) {
            res.set_content("请输入搜索关键字", "text/plain; charset=utf-8");
            return;
        }
        std::string word = req.get_param_value("word");
        LOG(LogLevel::INFO) << req.remote_addr << ":" << req.remote_port << " 用户搜索: " << word;
        std::string json_string;
        searcher.Search(word, &json_string);
        res.set_content(json_string, "application/json");
    });

    svr.Get("/doc", [&](const httplib::Request &req, httplib::Response &res) {
        if (!req.has_param("path")) {
            LOG(LogLevel::WARNING) << req.remote_addr << ":" << req.remote_port << " 获取文章失败: 缺少 path 参数";
            res.status = 400;
            res.set_content("缺少 path 参数", "text/plain; charset=utf-8");
            return;
        }

        std::string doc_path = NormalizeDocPath(req.get_param_value("path"));
        if (!IsSafeDocPath(doc_path)) {
            LOG(LogLevel::WARNING) << req.remote_addr << ":" << req.remote_port << " 获取文章失败: 非法文档路径 "
                                   << doc_path;
            res.status = 400;
            res.set_content("非法文档路径", "text/plain; charset=utf-8");
            return;
        }

        std::string content;
        std::filesystem::path file_path = std::filesystem::u8path(doc_path);
        if (!File_Util::ReadFileLines(file_path, &content)) {
            LOG(LogLevel::WARNING) << req.remote_addr << ":" << req.remote_port << " 获取文章失败: 文档不存在 "
                                   << doc_path;
            res.status = 404;
            res.set_content("文档不存在", "text/plain; charset=utf-8");
            return;
        }

        LOG(LogLevel::INFO) << req.remote_addr << ":" << req.remote_port << " 获取文章: " << doc_path;
        res.set_content(content, "text/plain; charset=utf-8");
    });

    svr.listen("0.0.0.0", 8080);
    return 0;
}
