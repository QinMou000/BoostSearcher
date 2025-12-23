/*
 * @Author: wang-qin928 2830862261@qq.com
 * @Date: 2025-10-09 21:52:01
 * @LastEditors: wang-qin928 2830862261@qq.com
 * @LastEditTime: 2025-12-23 11:20:42
 * @FilePath: \boost-searcher\http_server.cc
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "searcher.hpp"
#include "httplib.h"
#include "log.hpp"
#include "daemon.hpp"

const std::string raw = "./data/raw_html/raw.txt"; // 解析完的内容
const std::string root_path = "./wwwroot";

<<<<<<< HEAD
int main() {
=======
int main()
{
    daemon(); // 守护进程化

    FileLogStrategy(); // 默认日志写到文件里 http.log

>>>>>>> 77b05d638f516fb3723e1d688b0ef4da4a9a5b5c
    ns_searcher::Searcher searcher;
    searcher.InitSearcher(raw);

    httplib::Server svr;
    svr.set_base_dir(root_path.c_str());

<<<<<<< HEAD
    svr.Get("/s", [&](const httplib::Request & req, httplib::Response & res) {
        if(!req.has_param("word")) {
            res.set_content("请输入搜索关键字 ", "text/plain; charset=utf-8");
            return ;
        }
        std::string word = req.get_param_value("word");
        // std::cout << "用户搜索:" << word << std::endl;
        LOG(INFO, "用户搜索: " + word);
        std::string json_string;
        searcher.Search(word, &json_string);
        res.set_content(json_string, "application/json");
    });
=======
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
>>>>>>> 77b05d638f516fb3723e1d688b0ef4da4a9a5b5c

    svr.listen("0.0.0.0", 8080);
    return 0;
}
