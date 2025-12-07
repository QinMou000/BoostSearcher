#include "searcher.hpp"
#include "httplib.h"
#include "log.hpp"
#include "daemon.hpp"

const std::string raw = "./data/raw_html/raw.txt"; // 解析完的内容
const std::string root_path = "./wwwroot";

int main()
{
    daemon(); // 守护进程化

    FileLogStrategy(); // 默认日志写到文件里 http.log

    ns_searcher::Searcher searcher;
    searcher.InitSearcher(raw);

    httplib::Server svr;
    svr.set_base_dir(root_path.c_str());

    svr.Get("/s", [&](const httplib::Request &req, httplib::Response &res)
            { 
                if(!req.has_param("word"))
                {
                    res.set_content("请输入搜索关键字 ", "text/plain; charset=utf-8");
                    return ;
                } 
                std::string word = req.get_param_value("word");
                // std::cout << "用户搜索:" << word << std::endl;
                LOG(LogLevel::INFO) << req.remote_addr << " 用户搜索: " + word;
                std::string json_string;
                searcher.Search(word,&json_string); 
                // if(json_string.empty())
                //     res.set_content("can't find this word","application/json");
                // else
                res.set_content(json_string,"application/json"); });

    svr.listen("0.0.0.0", 8080);
    return 0;
}
