#include "searcher.hpp"
#include "httplib.h"
#include "log.hpp"
#include "daemon.hpp"

const std::string raw = "../../data/raw.txt";
const std::string root_path = "../../wwwroot";

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
        LOG(LogLevel::INFO) << "用户搜索: " + word;
        std::string json_string;
        searcher.Search(word, &json_string);
        res.set_content(json_string, "application/json");
    });

    svr.listen("0.0.0.0", 8080);
    return 0;
}
